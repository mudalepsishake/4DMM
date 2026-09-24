/* BRender:
 * Copyright (c) 1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: token.c 1.1 1997/12/10 16:41:11 jon Exp $
 * $Locker: $
 *
 * Token management and manipulation
 */
#include "fw.h"
#include "brassert.h"

/* BRender:
 * Private structure used to reference a registered token
 */
typedef struct br_token_entry {
    br_node   node;        /* BRender: Linked list  						*/
    char     *identifier;  /* BRender: Token name (including type suffix)	*/
    br_int_32 type;        /* BRender: Type of token						*/
    br_token  token;       /* BRender: Actual token value					*/
    br_int_32 base_length; /* BRender: Length of base identifier w/o type	*/
} br_token_entry;

/* BRender:
 * Predefined entries
 */
static br_token_entry predefinedTokens[] = {
#include "pretok.c"
};

/* BRender:
 * Token Types
 *
 * Type table, sorted by suffix length, largest first
 */
struct token_type {
    char     *identifier;
    br_token  type;
    br_size_t length;
    br_size_t size;
};

static struct token_type tokenTypes[] = {
#include "toktype.c"
};

/* BRenderModern:
 * Helper to make finding predefined tokens faster.
 */
static const br_token_entry *TokenEntryFind(br_token t)
{
    const br_token_entry *te;

    /* BRenderModern:
     * We're predefined, get directly.
     */
    if(t < BR_ASIZE(predefinedTokens))
        return predefinedTokens + t;

    /* BRenderModern:
     * We're custom, iterate backwards until either
     * we find it or hit our last predefined.
     */
    BR_FOR_LIST_R(&fw.tokens, te) {
        if(te->token == NEXT_FREE_TOKEN - 1)
            break;

        if(te->token == t)
            return te;
    }

    return predefinedTokens + BR_NULL_TOKEN;
}

/* BRender:
 * Initialise the token list
 */
void BrTokenBegin(void)
{
    /* BRender:
     * Set up token list
     */
    BrNewList(&fw.tokens);

    /* BRenderModern:
     * Add all the predefined tokens (except the NULL one).
     * Iterating through these using BR_FOR_LIST() should yield ascending order.
     */
    for(br_size_t i = 1; i < BR_ASIZE(predefinedTokens); i++)
        BR_ADDTAIL(&fw.tokens, predefinedTokens + i);

    /* BRender:
     * Setup next unused token id
     */
    fw.next_free_token = NEXT_FREE_TOKEN;
}

/* BRender:
 * Convert a token name into a number, creating a new one if necessary
 */
br_token BR_RESIDENT_ENTRY BrTokenCreate(const char *identifier, br_token type)
{
    br_token_entry *te;
    br_size_t       i, l;

    l = BrStrLen(identifier);

    /* BRenderModern:
     * See if name already exists, if so, return that.
     * Iterate in reverse to check custom tokens first.
     */
    BR_FOR_LIST_R(&fw.tokens, te)
        if(!BrStrCmp(identifier, te->identifier))
            return te->token;

    if(type == BR_NULL_TOKEN) {
        /* BRender:
         * If a null type is given, try to infer the type from the identifier's suffix
         *
         * This relies on the type table being sorted with longest suffixes first
         */

        type = BRT_NONE;

        for(i = 0; i < BR_ASIZE(tokenTypes); i++) {
            if(tokenTypes[i].length >= l)
                continue;

            if(!BrStrNCmp(identifier + l - tokenTypes[i].length, tokenTypes[i].identifier, tokenTypes[i].length)) {
                type = tokenTypes[i].type;
                break;
            }
        }
    } else {
        /* BRender:
         * If a type is given, but suffix does not match, generate an error
         */
        for(i = 0; i < BR_ASIZE(tokenTypes); i++) {
            if(tokenTypes[i].type == type)
                break;
        }

        if(i >= BR_ASIZE(tokenTypes))
            /* BRender:
             * Invalid type
             */
            return BR_NULL_TOKEN;

        if(tokenTypes[i].length >= l)
            return BR_NULL_TOKEN;

        if(BrStrNCmp(identifier + l - tokenTypes[i].length, tokenTypes[i].identifier, tokenTypes[i].length)) {
            return BR_NULL_TOKEN;
        }
    }

    /* BRender:
     * New name, add to list
     */
    te              = BrResAllocate(fw.res, sizeof(*te), BR_MEMORY_TOKEN);
    te->identifier  = BrResStrDup(te, identifier);
    te->base_length = l - tokenTypes[i].length;
    te->type        = type;
    te->token       = fw.next_free_token++;

    BR_ADDTAIL(&fw.tokens, te);

    return te->token;
}

/* BRender:
 * Returns a pointer to the identifier for the token
 */
char *BR_RESIDENT_ENTRY BrTokenIdentifier(br_token t)
{
    return TokenEntryFind(t)->identifier;
}

/* BRender:
 * Returns a token representing the type associated with the token
 */
br_token BR_RESIDENT_ENTRY BrTokenType(br_token t)
{
    return TokenEntryFind(t)->type;
}

/* BRender:
 * Count the number of registered tokens maching the given pattern
 */
br_int_32 BR_RESIDENT_ENTRY BrTokenCount(const char *pattern)
{
    br_token_entry *te;
    int             n = 0;

    if(pattern) {
        BR_FOR_LIST(&fw.tokens, te)
            if(BrNamePatternMatch(pattern, te->identifier))
                n++;
    } else {
        BR_FOR_LIST(&fw.tokens, te)
            n++;
    }
    return n;
}

/* BRenderModern:
 * Get the minimum size required to the given token value
 */
br_size_t BR_RESIDENT_ENTRY BrTokenSize(br_token t)
{
    if(t == BR_NULL_TOKEN)
        return 0;

    for(br_size_t i = 0; i < BR_ASIZE(tokenTypes); ++i) {
        if(tokenTypes[i].type == t)
            return tokenTypes[i].size;
    }

    return 0;
}

/* BRender:
 * Find the first token matching the given pattern
 */
br_token BR_RESIDENT_ENTRY BrTokenFind(const char *pattern)
{
    br_token_entry *te;

    /* BRender:
     * Find item in list
     */
    BR_FOR_LIST(&fw.tokens, te)
        if(BrNamePatternMatch(pattern, te->identifier))
            return te->token;

    return BR_NULL_TOKEN;
}

/* BRender:
 * Find all the tokens matching a given pattern
 */
br_int_32 BR_RESIDENT_ENTRY BrTokenFindMany(const char *pattern, br_token *tokens, br_int_32 max_tokens)
{
    br_token_entry *te;
    int             n = 0;

    /* BRender:
     * Find all matching items in list
     */
    BR_FOR_LIST(&fw.tokens, te) {
        /* BRender:
         * make sure there is space in output table
         */
        if(n >= max_tokens)
            break;

        /* BRender:
         * If entry matches, add to table
         */
        if(BrNamePatternMatch(pattern, te->identifier)) {
            *tokens++ = te->token;
            n++;
        }
    }

    return n;
}

/* BRender:
 * Internal special case of find - look for a token given the base name and a list of acceptable types
 * (in order of importance)
 */
br_token BrTokenFindType(br_token *ptype, const char *base, const br_token *types, br_size_t ntypes)
{
    br_token_entry *te;
    br_size_t       l, t;

    l = BrStrLen(base);

    ASSERT(l >= 1);

    /* BRender:
     * Find all matching items in list
     */
    BR_FOR_LIST(&fw.tokens, te) {
        /* BRender:
         * Check the basename
         */
        if(l != te->base_length)
            continue;

        if(BrStrNICmp(te->identifier, base, te->base_length))
            continue;

        /* BRender:
         * Look through list of acceptable types
         */
        for(t = 0; t < ntypes; t++) {
            if(te->type == types[t]) {
                /* BRender:
                 * Got a match
                 */
                if(ptype)
                    *ptype = te->type;

                return te->token;
            }
        }
    }

    return BR_NULL_TOKEN;
}
