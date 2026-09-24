/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    body.cpp: Body class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    A BODY holds the BRender-related data structures that make up what
    Socrates calls an actor.  The BODY keeps track of all the body parts'
    models, matrices, and materials that make up the actor's shape,
    position, orientation, and costume.  ACTR and TMPL are the main
    clients of BODY.  From the client's point of view, a BODY consists
    not of a tree of body parts, but an array of parts and part sets.  The
    "ibact"s and "ibset"s in the BODY APIs are indices into these arrays.

    PbodyNew() takes a parameter called pglibactPar, which is a GL of
    shorts.  Each short is the body part number of a body part's parent
    body part.	For example, suppose you passed in a pglibactPar of:

    (ivNil, 0, 0, 1, 2, 2)

    Body part 0 would have no parent (the first number in the GL is
    always ivNil).  Body part 1's parent would be body part 0.  Body part
    2's	parent would also be body part 0.  Body part 3's parent would be
    body part 1, etc.  The resulting tree would be:

                0
                |
             +--+--+
             |	   |
             1   +-2-+
             |	 |   |
             3	 4	 5

    BODY needs to keep track of three sets of BACTs: first, a root BACT
    whose transformation is changed to position and orient the BODY.
    Second, a BACT for the actor hilighting.  Finally, there are
    the body parts for the BODY.  So the BRender tree *really* looks like:

                            root
                              |
                       +------+-----+
                       |			|
                     part0		 hilite
                       |
               +-------+--------+
               |		        |
             part1            part2
               |	            |
               |		    +---+---+
               |		    |	    |
             part3	      part4	  part5


    All these BACTs are allocated and released in one _prgbact.  The
    root is _prgbact[0].  The hilite BACT is _prgbact[1].  The real
    body parts are _prgbact[2] through _prgbact[1 + 1 + _cbactPart].
    There are APIs to access various members of this array more easily.

    The second parameter to PbodyNew is pglibset, which divides the body
    parts into "body part sets."  A body part set is one or more body
    parts which are texture-mapped as a group.  For example, putting
    a shirt on a human would affect the torso and both arms, so those
    three body parts would be grouped into a body part set.  If body
    parts 0, 1, and 2 were in set 0; part 3 was in set 1; and parts 4
    and 5 were in set 2, _pglibset would be {0, 0, 0, 1, 2, 2}.

    When applying a MTRL to a body part set, the same MTRL is
    attached to each body part in the set.  When a CMTL is applied,
    the CMTL has a different MTRL per body part in the set.

    The way that MODLs, MTRLs, and CMTLs attach to the BODY is a little
    strange.  In the case of models, the br_actor's model needs to be set
    to the MODL's BMDL (which is a BRender data structure), not the
    MODL itself (which is a Socrates data structure).  When it is time
    to remove the model, the BODY has a pointer to the BMDL, but
    not the MODL!  Fortunately, MODL sets the BMDL's identifier field
    to point to its owning MODL.  So ReleasePpo can then be called on the
    MODL.  But I don't want to set the BMDL's identifier to pvNil in
    the process, because other BODYs might be counting on the BMDL's
    identifier to point to the MODL.  So this code has to be careful using
    ReleasePpo, since that sets the given pointer to pvNil.  Same
    problem for MTRLs:

    BODY
     |					 MODL<--+
     v					   |	|
     BACT   			   |	|
      | |				   |	|
      |	v				   v	|
      |model------------>BMDL	|
      |					   |	|
      |					   v	|
      |					  identifier
      |
      +--+
         |			     MTRL<--+
         |                 |	|
         v				   v	|
       material--------->BMTL	|
                           |	|
                           v	|
                          identifier

    CMTLs are potentially even messier, since they're an abstraction on
    top of MTRLs.  Here, an array of PCMTLs on the BODY is explicitly kept
    so it is easy to find out what CMTL is attached to what body part set
    instead of working backwards from the BACT's material field.

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(BODY)
RTCLASS(COST)

// 3DMMEx: The Explosion prop contains unused custom materials. This causes assert failures.
// 3DMMEx: FUTURE: Remove unused custom materials from the Explosion prop.
#define IGNORE_UNUSED_CUSTOM_MATERIALS

// 3DMMv1.0: Specification of hilite color.  REVIEW *****: should these
// 3DMMv1.0: values (or the PBMTL itself?) be passed in by the client?
const br_colour kbrcHilite = BR_COLOUR_RGB(255, 255, 255);
const uint8_t kbOpaque = 0xff;
const br_ufraction kbrufKaHilite = BR_UFRACTION(0.10);
const br_ufraction kbrufKdHilite = BR_UFRACTION(0.60);
const br_ufraction kbrufKsHilite = BR_UFRACTION(0.60);
const BRS krPowerHilite = BR_SCALAR(50);
const int32_t kiclrHilite = 0; // 3DMMv1.0: default to no highlighting

// 3DMMv1.0: Hilighting material
PBMTL BODY::_pbmtlHilite = pvNil;

// -multi Object Groups use a magenta bounding-edge material so an object that
// already belongs to a group can be identified without recolouring every other
// selected BODY. Each BODY owns its own hilite BACT, so switching that BACT's
// material pointer is enough; the BODY class layout does not change.
static PBMTL vpbmtl4DMMGroupedHilite = pvNil;
static bool vf4DMMGroupedHiliteNext = fFalse;

// Actor Studio has one private preview BODY at a time. Keep its selected part
// outside BODY so the legacy BODY object layout/serialization ABI remains
// untouched. _PrepareToRender recomputes the box from the live part matrix on
// every render, so action/frame changes move the box with the actual part.
static PBODY vpbody4DMMPartHilite = pvNil;
static int32_t vipart4DMMPartHilite = ivNil;
static int32_t vcpart4DMMPartHilite = 0;
// A default Actor/Prop Pt Group is represented by one hierarchy anchor row,
// but its visual selection is the complete BODY subtree rooted at that anchor.
// Keep this runtime-only just like the existing Actor Studio part highlight state.
static bool vf4DMMPartHiliteTree = fFalse;

void Set4DMMGroupedHiliteForNextBody(bool fGrouped)
{
    vf4DMMGroupedHiliteNext = FPure(fGrouped);
}

static PBMTL Pbmtl4DMMGroupedHilite(void)
{
    if (vpbmtl4DMMGroupedHilite != pvNil)
        return vpbmtl4DMMGroupedHilite;

    PBMTL pbmtl = BrMaterialAllocate(pvNil);
    if (pbmtl == pvNil)
        return pvNil;
    pbmtl->colour = BR_COLOUR_RGB(255, 0, 255);
    pbmtl->ka = kbrufKaHilite;
    pbmtl->kd = kbrufKdHilite;
    pbmtl->ks = kbrufKsHilite;
    pbmtl->power = krPowerHilite;
    // Group membership feedback should stay readable regardless of scene
    // lighting, just like the default flat-yellow 4DMM selection box.
    pbmtl->flags = BR_MATF_FORCE_Z_0;
    pbmtl->index_base = kiclrNormalHilite;
    pbmtl->index_range = 1;
    BrMaterialAdd(pbmtl);
    BrMaterialUpdate(pbmtl, BR_MATU_RENDERING | BR_MATU_LIGHTING);
    vpbmtl4DMMGroupedHilite = pbmtl;
    return pbmtl;
}

// Access shim for the runtime group-parent bridge. BODY follows the Socrates
// convention of keeping implementation fields protected; this derived helper
// exposes only the four internals needed by the body.cpp-local bridge without
// changing BODY's public class layout or header ABI.
class BODY4DMMGROUPACCESS : public BODY
{
  public:
    static PBACT Root(PBODY pbody)
    {
        return ((BODY4DMMGROUPACCESS *)pbody)->_PbactRoot();
    }
    static int32_t Hidden(PBODY pbody)
    {
        return ((BODY4DMMGROUPACCESS *)pbody)->_cactHidden;
    }
    static PBWLD World(PBODY pbody)
    {
        return ((BODY4DMMGROUPACCESS *)pbody)->_pbwld;
    }
    static uint32_t &ShadowProperties(PBODY pbody)
    {
        return ((BODY4DMMGROUPACCESS *)pbody)->_grf4DMMShadowProperties;
    }
};

enum
{
    kgrf4DMMShadowNoCastSystem = 0x00000001,
    kgrf4DMMShadowNoCastObject = 0x00000002,
    kgrf4DMMShadowFlushOverlap = 0x00000004
};

static void Sync4DMMBodyShadowRoot(PBODY pbody)
{
    if (pbody == pvNil)
        return;

    PBACT pbactRoot = BODY4DMMGROUPACCESS::Root(pbody);
    if (pbactRoot == pvNil || pbactRoot->type != BR_ACTOR_NONE)
        return;

    uint32_t &grf = BODY4DMMGROUPACCESS::ShadowProperties(pbody);
    pbactRoot->type_data = grf != 0 ? (void *)&grf : pvNil;
}

// Light Lab attachments are ordinary visible 3DMM objects used as authoring
// handles for their bound BRender lights.  Keep this system exclusion separate
// from the user-facing Object Properties shadow-casting toggle so refreshing
// Light Lab can never accidentally re-enable an object the user disabled.
void Set4DMMBodyShadowCasterExcluded(PBODY pbody, bool fExclude)
{
    if (pbody == pvNil)
        return;
    uint32_t &grf = BODY4DMMGROUPACCESS::ShadowProperties(pbody);
    if (fExclude)
        grf |= kgrf4DMMShadowNoCastSystem;
    else
        grf &= ~kgrf4DMMShadowNoCastSystem;
    Sync4DMMBodyShadowRoot(pbody);
}

void Set4DMMBodyObjectShadowProperties(PBODY pbody, bool fCastShadows, bool fFlushOverlap)
{
    if (pbody == pvNil)
        return;
    uint32_t &grf = BODY4DMMGROUPACCESS::ShadowProperties(pbody);
    if (fCastShadows)
        grf &= ~kgrf4DMMShadowNoCastObject;
    else
        grf |= kgrf4DMMShadowNoCastObject;
    if (fFlushOverlap)
        grf |= kgrf4DMMShadowFlushOverlap;
    else
        grf &= ~kgrf4DMMShadowFlushOverlap;
    Sync4DMMBodyShadowRoot(pbody);
}

// Bound -multi groups now use one real BRender parent actor. BODY still
// receives world-space transforms from the legacy ACTR engine, so remember
// the shared parent on the BODY root's otherwise-unused `user` field and
// convert those world transforms into parent-local transforms here.
//
// A group parent marks itself by storing its own address in `user`. BODY roots
// store the parent address instead, so the two cases cannot be confused.
static bool _F4DMMObjectGroupParentActor(PBACT pbact)
{
    return pbact != pvNil && pbact->type == BR_ACTOR_NONE && pbact->user == pbact;
}

static PBACT _P4DMMObjectGroupParent(PBODY pbody)
{
    if (pbody == pvNil)
        return pvNil;
    PBACT pbactRoot = BODY4DMMGROUPACCESS::Root(pbody);
    PBACT pbactParent = (PBACT)pbactRoot->user;
    return _F4DMMObjectGroupParentActor(pbactParent) ? pbactParent : pvNil;
}

static void _Get4DMMBodyWorldMatrix(PBODY pbody, BMAT34 *pbmat34)
{
    AssertPo(pbody, 0);
    AssertVarMem(pbmat34);

    PBACT pbactRoot = BODY4DMMGROUPACCESS::Root(pbody);
    PBACT pbactParent = _P4DMMObjectGroupParent(pbody);
    if (pbactParent == pvNil)
    {
        BrMatrix34Copy(pbmat34, &pbactRoot->t.t.mat);
        return;
    }

    // Object Group parents are direct children of BWLD's world actor. Their
    // matrix is therefore already parent-to-world.
    BrMatrix34Mul(pbmat34, &pbactRoot->t.t.mat, &pbactParent->t.t.mat);
}

// Exported only for movie.cpp. Keeping the attachment mechanics in body.cpp
// avoids changing BODY's public class layout while still letting BODY::Show,
// LocateOrient(), bounds and picking remain aware of the shared parent.
bool F4DMMBodyAttachToObjectGroupParent(PBODY pbody, PBACT pbactParent)
{
    AssertPo(pbody, 0);
    AssertVarMem(pbactParent);
    if (pbody == pvNil || !_F4DMMObjectGroupParentActor(pbactParent))
        return fFalse;

    PBACT pbactRoot = BODY4DMMGROUPACCESS::Root(pbody);
    BMAT34 bmat34World;
    _Get4DMMBodyWorldMatrix(pbody, &bmat34World);

    if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0 && pbactRoot->prev != pvNil)
        BrActorRemove(pbactRoot);

    BMAT34 bmat34ParentInv;
    if (BrMatrix34Inverse(&bmat34ParentInv, &pbactParent->t.t.mat) == rZero)
    {
        pbactRoot->user = pvNil;
        BrMatrix34Copy(&pbactRoot->t.t.mat, &bmat34World);
        if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0)
            BODY4DMMGROUPACCESS::World(pbody)->AddActor(pbactRoot);
        return fFalse;
    }

    pbactRoot->user = pbactParent;
    BrMatrix34Mul(&pbactRoot->t.t.mat, &bmat34World, &bmat34ParentInv);
    pbactRoot->t.type = BR_TRANSFORM_MATRIX34;
    if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0)
        BrActorAdd(pbactParent, pbactRoot);

    if (BODY4DMMGROUPACCESS::World(pbody) != pvNil)
        BODY4DMMGROUPACCESS::World(pbody)->MarkDirty();
    return fTrue;
}

void Detach4DMMBodyFromObjectGroupParent(PBODY pbody)
{
    AssertPo(pbody, 0);
    if (pbody == pvNil)
        return;

    PBACT pbactRoot = BODY4DMMGROUPACCESS::Root(pbody);
    PBACT pbactGroupParent = _P4DMMObjectGroupParent(pbody);

    /*
     * v234: Object Properties add persistent BODY-root metadata. Object Group
     * teardown must therefore leave both hierarchy attachment and that root
     * metadata authoritative. A visible BODY with no runtime parent is always
     * stranded and would disappear even though its ACTR still exists.
     */
    if (pbactGroupParent == pvNil)
    {
        if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0 && pbactRoot->parent == pvNil &&
            BODY4DMMGROUPACCESS::World(pbody) != pvNil)
        {
            BODY4DMMGROUPACCESS::World(pbody)->AddActor(pbactRoot);
#if defined(BRENDER_MODERN_14)
            BrModernLog("OBJECT GROUP v234 repaired stranded BODY root body=%p root=%p", pbody, pbactRoot);
#endif
        }
        Sync4DMMBodyShadowRoot(pbody);
        return;
    }

    BMAT34 bmat34World;
    _Get4DMMBodyWorldMatrix(pbody, &bmat34World);

    if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0 && pbactRoot->prev != pvNil)
        BrActorRemove(pbactRoot);

    pbactRoot->user = pvNil;
    BrMatrix34Copy(&pbactRoot->t.t.mat, &bmat34World);
    pbactRoot->t.type = BR_TRANSFORM_MATRIX34;

    if (BODY4DMMGROUPACCESS::Hidden(pbody) == 0 && BODY4DMMGROUPACCESS::World(pbody) != pvNil)
        BODY4DMMGROUPACCESS::World(pbody)->AddActor(pbactRoot);

    // Re-publish shadow/object policy after reparenting. This is intentionally
    // separate from hierarchy state so Abandon/Unbind cannot lose a non-default
    // Object Properties record or leave type_data pointing at stale policy.
    Sync4DMMBodyShadowRoot(pbody);

    if (BODY4DMMGROUPACCESS::World(pbody) != pvNil)
        BODY4DMMGROUPACCESS::World(pbody)->MarkDirty();
}

// Capture the complete child-local matrix after BODY has been attached to a
// shared Object Group parent. This includes BODY-level rest orientation and
// size/stretch state that is deliberately not part of ACTR::bmat34Cur.
bool F4DMMBodyGetObjectGroupLocalPose(PBODY pbody, BMAT34 *pbmat34Local)
{
    AssertPo(pbody, 0);
    AssertVarMem(pbmat34Local);
    if (pbody == pvNil || pbmat34Local == pvNil || _P4DMMObjectGroupParent(pbody) == pvNil)
        return fFalse;

    BrMatrix34Copy(pbmat34Local, &BODY4DMMGROUPACCESS::Root(pbody)->t.t.mat);
    return fTrue;
}

// Once an Object Group is bound, its member BODY roots are children of one
// shared parent. Legacy ACTR events still need to be updated for save/undo,
// but those updates must not become a second render-time transform authority.
// Restore the immutable bind-local BODY matrix after the ACTR bookkeeping so
// the visible hierarchy is exactly parent * frozen child.
bool F4DMMBodySetObjectGroupLocalPose(PBODY pbody, const BMAT34 *pbmat34Local)
{
    AssertPo(pbody, 0);
    AssertVarMem(pbmat34Local);
    if (pbody == pvNil || pbmat34Local == pvNil || _P4DMMObjectGroupParent(pbody) == pvNil)
        return fFalse;

    BrMatrix34Copy(&BODY4DMMGROUPACCESS::Root(pbody)->t.t.mat, pbmat34Local);
    BODY4DMMGROUPACCESS::Root(pbody)->t.type = BR_TRANSFORM_MATRIX34;
    if (BODY4DMMGROUPACCESS::World(pbody) != pvNil)
        BODY4DMMGROUPACCESS::World(pbody)->MarkDirty();
    return fTrue;
}

// 4DMM selection-box presentation for the -c -a -l path.  Vanilla 3DMM
// gets its yellow from palette index 108; RGB888 highlighting instead sees
// br_material::colour, which is why the recent lighting work left the box
// white/grey and light-reactive.  Keep the material shared exactly as before,
// but let Alt+Y choose how that one material is presented.
//
// 0 = flat original-style yellow (default)
// 1 = yellow with the current lighting/Gouraud response
// 2 = current experimental white/light-reactive appearance
static int32_t _imod4dmmHilite = 0;

static bool _F4dmmHiliteModesActive(void)
{
    return BWLD::FTrueColorMode() && BWLD::FActorLightMode() && MVIE::FTestLightMode();
}

static void _Apply4dmmHiliteMaterial(PBMTL pbmtl)
{
    if (pbmtl == pvNil || !_F4dmmHiliteModesActive())
        return;

    const br_colour brcYellow = BR_COLOUR_RGB(250, 210, 27);

    switch (_imod4dmmHilite)
    {
    default:
    case 0:
        // Original-style visibility: one solid yellow, deliberately unaffected
        // by scene lights so the selection remains readable in a busy shot.
        pbmtl->colour = brcYellow;
        pbmtl->flags = BR_MATF_FORCE_Z_0;
        break;

    case 1:
        // Same yellow hue, but allow the experimental lighting path to shade
        // the bounding edges exactly as it shades the current selection box.
        pbmtl->colour = brcYellow;
        pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD | BR_MATF_FORCE_Z_0;
        break;

    case 2:
        // Preserve the pre-toggle actor-light behavior for comparison.
        pbmtl->colour = kbrcHilite;
        pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD | BR_MATF_FORCE_Z_0;
        break;
    }

    // Keep the indexed fallback authored as the stock selection yellow.  The
    // RGB value above is what matters in -c, but retaining index 108 makes the
    // shared material sane if a render path consults the indexed fields.
    pbmtl->index_base = kiclrNormalHilite;
    pbmtl->index_range = 1;
    BrMaterialUpdate(pbmtl, BR_MATU_RENDERING | BR_MATU_LIGHTING);
}

PBODY BODY::_pbodyClosestClicked;
int32_t BODY::_dzpClosestClicked;
PBACT BODY::_pbactClosestClicked;

// Taxi perspective correction.
//
// The stock taxi body UVs are already a nearly exact planar X/Y projection.
// The direction-dependent door-line warping is therefore not crooked source
// artwork and another planar remap would be nearly identical to the original.
//
// The old software renderer is affinely mapping each large triangle. That is
// exact in a side view because every point on the side panel has almost the
// same camera depth, but it becomes visibly wrong when the taxi is rotated or
// brought close to the camera. Subdivide the side-facing triangles, the four
// enormous sloped shoulder triangles under the windows, and the transverse
// wheel-arch bevel triangles so each affine approximation covers a much
// smaller area. Keeping the same subdivision depth on both sides of their
// shared edges also removes the moving one-pixel cracks that appeared where
// repaired panels met untouched transition strips.
static char _bTaxiPerspectiveSubdividedModel;

// actorlight24: broad rejection categories for the prepared-geometry fallback.
// performance.csv stores an OR-mask of these reasons per frame so we can see
// exactly which layer differs without logging hundreds of per-part lines.
enum
{
    kgeomrejNil = 0,
    kgeomrejNull = 1,
    kgeomrejInstanceState = 2,
    kgeomrejLayout = 3,
    kgeomrejBounds = 4,
    kgeomrejVertexPosition = 5,
    kgeomrejVertexNormal = 6,
    kgeomrejVertexMap = 7,
    kgeomrejVertexPrelight = 8,
    kgeomrejFaceMaterial = 9,
    kgeomrejFaceTopology = 10,
    kgeomrejFaceNormal = 11,
    kgeomrejFaceState = 12,
    kgeomrejVertexGroup = 13,
    kgeomrejFaceGroup = 14
};

static bool _FSamePreparedModelGeometry(PBMDL pbmdlA, PBMDL pbmdlB, int32_t *pirej)
{
    if (pirej != pvNil)
        *pirej = kgeomrejNil;
    if (pvNil == pbmdlA || pvNil == pbmdlB)
    {
        if (pirej != pvNil)
            *pirej = kgeomrejNull;
        return fFalse;
    }

    // Generated/custom BRender models may carry instance-specific state that
    // must never be aliased merely because their visible geometry happens to
    // match at this instant.
    if (pbmdlA->user != pvNil || pbmdlB->user != pvNil ||
        pbmdlA->custom != pvNil || pbmdlB->custom != pvNil ||
        pbmdlA->flags != pbmdlB->flags)
    {
        if (pirej != pvNil)
            *pirej = kgeomrejInstanceState;
        return fFalse;
    }

    if (pbmdlA->nprepared_vertices != pbmdlB->nprepared_vertices ||
        pbmdlA->nprepared_faces != pbmdlB->nprepared_faces ||
        pbmdlA->nface_groups != pbmdlB->nface_groups ||
        pbmdlA->nvertex_groups != pbmdlB->nvertex_groups ||
        pbmdlA->nedges != pbmdlB->nedges ||
        pbmdlA->prep_flags != pbmdlB->prep_flags ||
        pbmdlA->prepared_vertices == pvNil || pbmdlB->prepared_vertices == pvNil ||
        pbmdlA->prepared_faces == pvNil || pbmdlB->prepared_faces == pvNil ||
        (pbmdlA->nvertex_groups > 0 &&
         (pbmdlA->vertex_groups == pvNil || pbmdlB->vertex_groups == pvNil)) ||
        (pbmdlA->nface_groups > 0 &&
         (pbmdlA->face_groups == pvNil || pbmdlB->face_groups == pvNil)))
    {
        if (pirej != pvNil)
            *pirej = kgeomrejLayout;
        return fFalse;
    }

    for (int32_t iaxis = 0; iaxis < 3; iaxis++)
    {
        if (pbmdlA->pivot.v[iaxis] != pbmdlB->pivot.v[iaxis] ||
            pbmdlA->bounds.min.v[iaxis] != pbmdlB->bounds.min.v[iaxis] ||
            pbmdlA->bounds.max.v[iaxis] != pbmdlB->bounds.max.v[iaxis])
        {
            if (pirej != pvNil)
                *pirej = kgeomrejBounds;
            return fFalse;
        }
    }
    if (pbmdlA->radius != pbmdlB->radius)
    {
        if (pirej != pvNil)
            *pirej = kgeomrejBounds;
        return fFalse;
    }

    for (int32_t ibrv = 0; ibrv < pbmdlA->nprepared_vertices; ibrv++)
    {
        const BRV &a = pbmdlA->prepared_vertices[ibrv];
        const BRV &b = pbmdlB->prepared_vertices[ibrv];

        for (int32_t iaxis = 0; iaxis < 3; iaxis++)
        {
            if (a.p.v[iaxis] != b.p.v[iaxis])
            {
                if (pirej != pvNil)
                    *pirej = kgeomrejVertexPosition;
                return fFalse;
            }
        }
        for (int32_t iaxis = 0; iaxis < 3; iaxis++)
        {
            if (a.n.v[iaxis] != b.n.v[iaxis])
            {
                if (pirej != pvNil)
                    *pirej = kgeomrejVertexNormal;
                return fFalse;
            }
        }
        for (int32_t iaxis = 0; iaxis < 2; iaxis++)
        {
            if (a.map.v[iaxis] != b.map.v[iaxis])
            {
                if (pirej != pvNil)
                    *pirej = kgeomrejVertexMap;
                return fFalse;
            }
        }
        if (a.index != b.index || a.red != b.red || a.grn != b.grn ||
            a.blu != b.blu || a.r != b.r)
        {
            if (pirej != pvNil)
                *pirej = kgeomrejVertexPrelight;
            return fFalse;
        }
    }

    for (int32_t ibrf = 0; ibrf < pbmdlA->nprepared_faces; ibrf++)
    {
        const BRF &a = pbmdlA->prepared_faces[ibrf];
        const BRF &b = pbmdlB->prepared_faces[ibrf];

        // Stock 3DMM model chunks do not store face-local materials. If a
        // model does, decline the optimization rather than treating material
        // pointers from different CRFs as equivalent.
        if (a.material != pvNil || b.material != pvNil)
        {
            if (pirej != pvNil)
                *pirej = kgeomrejFaceMaterial;
            return fFalse;
        }

        for (int32_t i = 0; i < 3; i++)
        {
#if defined(BRENDER_MODERN_14)
            // BRender 1.4 regenerates edge topology in its private prepared
            // model and no longer exposes the 1995 face edge indices. Vertex
            // topology is the stable geometry identity needed here.
            if (a.vertices[i] != b.vertices[i])
#else
            if (a.vertices[i] != b.vertices[i] || a.edges[i] != b.edges[i])
#endif
            {
                if (pirej != pvNil)
                    *pirej = kgeomrejFaceTopology;
                return fFalse;
            }
        }
        for (int32_t i = 0; i < 3; i++)
        {
            if (a.n.v[i] != b.n.v[i])
            {
                if (pirej != pvNil)
                    *pirej = kgeomrejFaceNormal;
                return fFalse;
            }
        }
        if (a.smoothing != b.smoothing || a.flags != b.flags || a.d != b.d)
        {
            if (pirej != pvNil)
                *pirej = kgeomrejFaceState;
            return fFalse;
        }
    }

    int32_t ibrvA = 0;
    int32_t ibrvB = 0;
    for (int32_t igrp = 0; igrp < pbmdlA->nvertex_groups; igrp++)
    {
        const br_vertex_group &a = pbmdlA->vertex_groups[igrp];
        const br_vertex_group &b = pbmdlB->vertex_groups[igrp];
        if (a.material != pvNil || b.material != pvNil ||
            a.nvertices != b.nvertices ||
            a.vertices != pbmdlA->prepared_vertices + ibrvA ||
            b.vertices != pbmdlB->prepared_vertices + ibrvB)
        {
            if (pirej != pvNil)
                *pirej = kgeomrejVertexGroup;
            return fFalse;
        }
        ibrvA += a.nvertices;
        ibrvB += b.nvertices;
    }
    if (ibrvA != pbmdlA->nprepared_vertices || ibrvB != pbmdlB->nprepared_vertices)
    {
        if (pirej != pvNil)
            *pirej = kgeomrejVertexGroup;
        return fFalse;
    }

    int32_t ibrfA = 0;
    int32_t ibrfB = 0;
    for (int32_t igrp = 0; igrp < pbmdlA->nface_groups; igrp++)
    {
        const br_face_group &a = pbmdlA->face_groups[igrp];
        const br_face_group &b = pbmdlB->face_groups[igrp];
        if (a.material != pvNil || b.material != pvNil ||
            a.nfaces != b.nfaces ||
            a.faces != pbmdlA->prepared_faces + ibrfA ||
            b.faces != pbmdlB->prepared_faces + ibrfB)
        {
            if (pirej != pvNil)
                *pirej = kgeomrejFaceGroup;
            return fFalse;
        }
        ibrfA += a.nfaces;
        ibrfB += b.nfaces;
    }
    if (ibrfA != pbmdlA->nprepared_faces || ibrfB != pbmdlB->nprepared_faces)
    {
        if (pirej != pvNil)
            *pirej = kgeomrejFaceGroup;
        return fFalse;
    }

    return fTrue;
}

static float _FlAbs(float fl)
{
    return fl < 0.0f ? -fl : fl;
}

static bool _FTaxiVertexAt(PBMDL pbmdl, int32_t ibrv, float x, float y, float z)
{
    BRV *pbrv = &pbmdl->prepared_vertices[ibrv];
    return _FlAbs(BrScalarToFloat(pbrv->p.v[0]) - x) < 0.01f &&
           _FlAbs(BrScalarToFloat(pbrv->p.v[1]) - y) < 0.01f &&
           _FlAbs(BrScalarToFloat(pbrv->p.v[2]) - z) < 0.01f;
}

static bool _FTaxiBodyModel(PBMDL pbmdl)
{
    if (pvNil == pbmdl || pbmdl->user == &_bTaxiPerspectiveSubdividedModel)
        return fFalse;

    if (MODL::FSourceChunk(pbmdl, kctgBmdl, 0x58))
        return fTrue;

    if (pvNil == pbmdl->prepared_vertices || pvNil == pbmdl->prepared_faces ||
        pbmdl->nprepared_vertices != 198 || pbmdl->nprepared_faces != 392)
    {
        return fFalse;
    }

    return _FTaxiVertexAt(pbmdl, 0, -22.03125f, 5.375f, -7.75f) &&
           _FTaxiVertexAt(pbmdl, 100, -13.4375f, 7.75f, -9.96875f) &&
           _FTaxiVertexAt(pbmdl, 197, 23.5625f, 10.25f, 1.0625f);
}

static bool _FTaxiSideFace(PBMDL pbmdl, BRF *pbrf)
{
    BRV *pbrv0 = &pbmdl->prepared_vertices[pbrf->vertices[0]];
    BRV *pbrv1 = &pbmdl->prepared_vertices[pbrf->vertices[1]];
    BRV *pbrv2 = &pbmdl->prepared_vertices[pbrf->vertices[2]];

    float ux = BrScalarToFloat(pbrv1->p.v[0] - pbrv0->p.v[0]);
    float uy = BrScalarToFloat(pbrv1->p.v[1] - pbrv0->p.v[1]);
    float uz = BrScalarToFloat(pbrv1->p.v[2] - pbrv0->p.v[2]);
    float vx = BrScalarToFloat(pbrv2->p.v[0] - pbrv0->p.v[0]);
    float vy = BrScalarToFloat(pbrv2->p.v[1] - pbrv0->p.v[1]);
    float vz = BrScalarToFloat(pbrv2->p.v[2] - pbrv0->p.v[2]);

    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float c2 = nx * nx + ny * ny + nz * nz;

    if (c2 <= 0.000001f)
        return fTrue;

    return nz * nz >= 0.81f * c2;
}

static bool _FTaxiDoorShoulderFace(PBMDL pbmdl, BRF *pbrf)
{
    BRV *pbrv0 = &pbmdl->prepared_vertices[pbrf->vertices[0]];
    BRV *pbrv1 = &pbmdl->prepared_vertices[pbrf->vertices[1]];
    BRV *pbrv2 = &pbmdl->prepared_vertices[pbrf->vertices[2]];

    float x0 = BrScalarToFloat(pbrv0->p.v[0]);
    float x1 = BrScalarToFloat(pbrv1->p.v[0]);
    float x2 = BrScalarToFloat(pbrv2->p.v[0]);
    float y0 = BrScalarToFloat(pbrv0->p.v[1]);
    float y1 = BrScalarToFloat(pbrv1->p.v[1]);
    float y2 = BrScalarToFloat(pbrv2->p.v[1]);
    float z0 = BrScalarToFloat(pbrv0->p.v[2]);
    float z1 = BrScalarToFloat(pbrv1->p.v[2]);
    float z2 = BrScalarToFloat(pbrv2->p.v[2]);

    float xMin = x0 < x1 ? x0 : x1;
    if (x2 < xMin)
        xMin = x2;
    float xMax = x0 > x1 ? x0 : x1;
    if (x2 > xMax)
        xMax = x2;
    float yMin = y0 < y1 ? y0 : y1;
    if (y2 < yMin)
        yMin = y2;
    float yMax = y0 > y1 ? y0 : y1;
    if (y2 > yMax)
        yMax = y2;
    float zMin = z0 < z1 ? z0 : z1;
    if (z2 < zMin)
        zMin = z2;
    float zMax = z0 > z1 ? z0 : z1;
    if (z2 > zMax)
        zMax = z2;
    float yCenter = (y0 + y1 + y2) / 3.0f;
    float zAbsMean = (_FlAbs(z0) + _FlAbs(z1) + _FlAbs(z2)) / 3.0f;

    // These bounds select exactly the four stock-taxi triangles forming the
    // long sloped strip between the vertical doors and the window bottoms.
    return xMax - xMin > 20.0f &&
           yMax - yMin > 0.5f &&
           zMax - zMin > 1.0f &&
           yCenter > 12.5f && yCenter < 13.8f &&
           zAbsMean > 8.0f;
}

static bool _FTaxiWheelArchFace(PBMDL pbmdl, BRF *pbrf)
{
    BRV *pbrv0 = &pbmdl->prepared_vertices[pbrf->vertices[0]];
    BRV *pbrv1 = &pbmdl->prepared_vertices[pbrf->vertices[1]];
    BRV *pbrv2 = &pbmdl->prepared_vertices[pbrf->vertices[2]];

    float x0 = BrScalarToFloat(pbrv0->p.v[0]);
    float x1 = BrScalarToFloat(pbrv1->p.v[0]);
    float x2 = BrScalarToFloat(pbrv2->p.v[0]);
    float y0 = BrScalarToFloat(pbrv0->p.v[1]);
    float y1 = BrScalarToFloat(pbrv1->p.v[1]);
    float y2 = BrScalarToFloat(pbrv2->p.v[1]);
    float z0 = BrScalarToFloat(pbrv0->p.v[2]);
    float z1 = BrScalarToFloat(pbrv1->p.v[2]);
    float z2 = BrScalarToFloat(pbrv2->p.v[2]);

    float xMin = x0 < x1 ? x0 : x1;
    if (x2 < xMin)
        xMin = x2;
    float xMax = x0 > x1 ? x0 : x1;
    if (x2 > xMax)
        xMax = x2;
    float zMin = z0 < z1 ? z0 : z1;
    if (z2 < zMin)
        zMin = z2;
    float zMax = z0 > z1 ? z0 : z1;
    if (z2 > zMax)
        zMax = z2;
    float xCenter = (x0 + x1 + x2) / 3.0f;
    float yCenter = (y0 + y1 + y2) / 3.0f;
    float xAbsCenter = _FlAbs(xCenter);

    // Keep the original wheel-arch strip coverage, then extend it across the
    // three large outer-quarter triangles that meet the subdivided side panel.
    // v46 stopped just inside these faces, leaving another T-junction where the
    // highlighted front-fender line crossed into the untouched quarter panel.
    bool fWheelArchStrip = zMax - zMin > 19.0f &&
                           xAbsCenter > 6.0f && xAbsCenter < 16.6f &&
                           yCenter > 3.5f && yCenter < 8.3f;
    bool fOuterQuarter = zMax - zMin > 17.0f &&
                         xMax - xMin > 1.0f &&
                         xAbsCenter >= 16.6f && xAbsCenter < 22.5f &&
                         yCenter > 3.2f && yCenter < 8.7f;
    return fWheelArchStrip || fOuterQuarter;
}

static bool _FTaxiPerspectiveFace(PBMDL pbmdl, BRF *pbrf)
{
    return _FTaxiSideFace(pbmdl, pbrf) ||
           _FTaxiDoorShoulderFace(pbmdl, pbrf) ||
           _FTaxiWheelArchFace(pbmdl, pbrf);
}

static BRV _BrvTaxiMidpoint(const BRV *pbrv0, const BRV *pbrv1)
{
    BRV brv;
    ClearPb(&brv, SIZEOF(brv));

    for (int32_t iax = 0; iax < 3; iax++)
    {
        brv.p.v[iax] = BR_MUL(BR_ADD(pbrv0->p.v[iax], pbrv1->p.v[iax]),
                              BR_SCALAR(0.5));
    }
    for (int32_t iax = 0; iax < 2; iax++)
    {
        brv.map.v[iax] = BR_MUL(BR_ADD(pbrv0->map.v[iax], pbrv1->map.v[iax]),
                                BR_SCALAR(0.5));
    }

    brv.index = (uint8_t)(((int32_t)pbrv0->index + (int32_t)pbrv1->index + 1) / 2);
    brv.red = (uint8_t)(((int32_t)pbrv0->red + (int32_t)pbrv1->red + 1) / 2);
    brv.grn = (uint8_t)(((int32_t)pbrv0->grn + (int32_t)pbrv1->grn + 1) / 2);
    brv.blu = (uint8_t)(((int32_t)pbrv0->blu + (int32_t)pbrv1->blu + 1) / 2);
    return brv;
}

static void _EmitTaxiTriangle(const BRV *pbrv0, const BRV *pbrv1, const BRV *pbrv2,
                              uint16_t grfsm, int32_t csub,
                              BRV *prgbrv, BRF *prgbrf,
                              int32_t *pibrv, int32_t *pibrf)
{
    if (csub > 0)
    {
        BRV brv01 = _BrvTaxiMidpoint(pbrv0, pbrv1);
        BRV brv12 = _BrvTaxiMidpoint(pbrv1, pbrv2);
        BRV brv20 = _BrvTaxiMidpoint(pbrv2, pbrv0);

        _EmitTaxiTriangle(pbrv0, &brv01, &brv20, grfsm, csub - 1,
                          prgbrv, prgbrf, pibrv, pibrf);
        _EmitTaxiTriangle(&brv01, pbrv1, &brv12, grfsm, csub - 1,
                          prgbrv, prgbrf, pibrv, pibrf);
        _EmitTaxiTriangle(&brv20, &brv12, pbrv2, grfsm, csub - 1,
                          prgbrv, prgbrf, pibrv, pibrf);
        _EmitTaxiTriangle(&brv01, &brv12, &brv20, grfsm, csub - 1,
                          prgbrv, prgbrf, pibrv, pibrf);
        return;
    }

    int32_t ibrv = *pibrv;
    int32_t ibrf = *pibrf;

    prgbrv[ibrv] = *pbrv0;
    prgbrv[ibrv + 1] = *pbrv1;
    prgbrv[ibrv + 2] = *pbrv2;

    ClearPb(&prgbrf[ibrf], SIZEOF(BRF));
    prgbrf[ibrf].vertices[0] = (uint16_t)ibrv;
    prgbrf[ibrf].vertices[1] = (uint16_t)(ibrv + 1);
    prgbrf[ibrf].vertices[2] = (uint16_t)(ibrv + 2);
    prgbrf[ibrf].material = pvNil;
    prgbrf[ibrf].smoothing = grfsm;
    prgbrf[ibrf].flags = 0;

    *pibrv += 3;
    *pibrf += 1;
}

static PMODL _PmodlTaxiPerspectiveSubdivided(PBMDL pbmdl)
{
    if (!_FTaxiBodyModel(pbmdl))
        return pvNil;

    const int32_t csubSide = 3;
    const int32_t cfacPerSideFace = 64;

    int32_t cbrfNew = 0;
    for (int32_t ibrf = 0; ibrf < pbmdl->nprepared_faces; ibrf++)
    {
        cbrfNew += _FTaxiPerspectiveFace(pbmdl, &pbmdl->prepared_faces[ibrf])
                       ? cfacPerSideFace
                       : 1;
    }

    int32_t cbrvNew = LwMul(cbrfNew, 3);
    if (cbrfNew <= 0 || cbrfNew >= ksuMax ||
        cbrvNew <= 0 || cbrvNew >= ksuMax)
    {
        return pvNil;
    }

    BRV *prgbrv = pvNil;
    BRF *prgbrf = pvNil;
    PMODL pmodl = pvNil;
    int32_t ibrvNew = 0;
    int32_t ibrfNew = 0;

    if (!FAllocPv((void **)&prgbrv, LwMul(cbrvNew, SIZEOF(BRV)), fmemClear, mprNormal) ||
        !FAllocPv((void **)&prgbrf, LwMul(cbrfNew, SIZEOF(BRF)), fmemClear, mprNormal))
    {
        goto LDone;
    }

    for (int32_t ibrf = 0; ibrf < pbmdl->nprepared_faces; ibrf++)
    {
        BRF *pbrf = &pbmdl->prepared_faces[ibrf];
        BRV *pbrv0 = &pbmdl->prepared_vertices[pbrf->vertices[0]];
        BRV *pbrv1 = &pbmdl->prepared_vertices[pbrf->vertices[1]];
        BRV *pbrv2 = &pbmdl->prepared_vertices[pbrf->vertices[2]];
        int32_t csub = _FTaxiPerspectiveFace(pbmdl, pbrf) ? csubSide : 0;

        _EmitTaxiTriangle(pbrv0, pbrv1, pbrv2, pbrf->smoothing, csub,
                          prgbrv, prgbrf, &ibrvNew, &ibrfNew);
    }

    Assert(ibrvNew == cbrvNew, "taxi subdivided vertex count mismatch");
    Assert(ibrfNew == cbrfNew, "taxi subdivided face count mismatch");

    pmodl = MODL::PmodlNew(cbrvNew, prgbrv, cbrfNew, prgbrf);
    if (pvNil != pmodl)
        pmodl->Pbmdl()->user = &_bTaxiPerspectiveSubdividedModel;

LDone:
    FreePpv((void **)&prgbrv);
    FreePpv((void **)&prgbrf);
    return pmodl;
}

/** 3DMMv1.0: *************************************************************************
    Builds a tree of BACTs for the BODY.
***************************************************************************/
BODY *BODY::PbodyNew(PGL pglibactPar, PGL pglibset)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::PbodyNew BEGIN parents=%p sets=%p parts=%ld/%ld", pglibactPar, pglibset,
                pglibactPar != pvNil ? (long)pglibactPar->IvMac() : -1L,
                pglibset != pvNil ? (long)pglibset->IvMac() : -1L);
#endif
    AssertPo(pglibactPar, 0);
    Assert(pglibactPar->CbEntry() == SIZEOF(int16_t), "bad pglibactPar");
    AssertPo(pglibset, 0);
    Assert(pglibset->CbEntry() == SIZEOF(int16_t), "bad pglibset");

    BODY *pbody;

    if (pvNil == _pbmtlHilite)
    {
        _pbmtlHilite = BrMaterialAllocate(pvNil);
        if (pvNil == _pbmtlHilite)
            return pvNil;
        _pbmtlHilite->colour = kbrcHilite;
        _pbmtlHilite->ka = kbrufKaHilite;
        _pbmtlHilite->kd = kbrufKdHilite, _pbmtlHilite->ks = kbrufKsHilite;
        _pbmtlHilite->power = krPowerHilite;
        _pbmtlHilite->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD | BR_MATF_FORCE_Z_0;
        _pbmtlHilite->index_base = kiclrHilite;
        _pbmtlHilite->index_range = 1;
        BrMaterialAdd(_pbmtlHilite);
        _Apply4dmmHiliteMaterial(_pbmtlHilite);
    }

    pbody = NewObj BODY;
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::PbodyNew allocated body=%p", pbody);
#endif
    if (pvNil == pbody || !pbody->_FInit(pglibactPar, pglibset))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BODY::PbodyNew FAIL body=%p", pbody);
#endif
        ReleasePpo(&pbody);
        return pvNil;
    }

#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::PbodyNew SUCCESS body=%p root=%p parts=%ld sets=%ld", pbody, pbody->_PbactRoot(),
                (long)pbody->_cbactPart, (long)pbody->_cbset);
#endif
    AssertPo(pbody, fobjAssertFull);
    return pbody;
}

/** 3DMMv1.0: *************************************************************************
    Build the BODY
***************************************************************************/
bool BODY::_FInit(PGL pglibactPar, PGL pglibset)
{
    AssertBaseThis(0);
    AssertPo(pglibactPar, 0);
    AssertPo(pglibset, 0);

    _cactHidden = 1; // 3DMMv1.0: body starts out hidden

    if (!_FInitShape(pglibactPar, pglibset))
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Build the BODY
***************************************************************************/
bool BODY::_FInitShape(PGL pglibactPar, PGL pglibset)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::_FInitShape BEGIN this=%p parts=%ld", this,
                pglibactPar != pvNil ? (long)pglibactPar->IvMac() : -1L);
#endif
    AssertBaseThis(0);
    AssertPo(pglibactPar, 0);
    Assert(pglibactPar->CbEntry() == SIZEOF(int16_t), "bad pglibactPar");
    AssertPo(pglibset, 0);
    Assert(pglibset->CbEntry() == SIZEOF(int16_t), "bad pglibset");
    Assert(pglibactPar->IvMac() == 0 || ivNil == *(int16_t *)pglibactPar->QvGet(0), "bad first item in pglibactPar");
    Assert(pglibactPar->IvMac() == pglibset->IvMac(), "pglibactPar must be same size as pglibset");

    BACT *pbact;
    BACT *pbactPar;
    int16_t ibactPar;
    int16_t ibact;
    int16_t ibset;

    // 3DMMv1.0: Copy pglibset into _pglibset
    _pglibset = pglibset->PglDup();
    if (pvNil == _pglibset)
        return fFalse;

    // 3DMMv1.0: _cbset is (highest entry in _pglibset) + 1.
    _cbset = -1;
    for (ibact = 0; ibact < _pglibset->IvMac(); ibact++)
    {
        _pglibset->Get(ibact, &ibset);
        if (ibset > _cbset)
            _cbset = ibset;
    }
    _cbset++;

    if (!FAllocPv((void **)&_prgpcmtl, LwMul(_cbset, SIZEOF(PCMTL)), fmemClear, mprNormal))
    {
        return fFalse;
    }

    _cbactPart = pglibactPar->IvMac();
    Assert(_cbset <= _cbactPart, "More sets than body parts?");
    if (!FAllocPv((void **)&_prgbact, LwMul(_Cbact(), SIZEOF(BACT)), fmemClear, mprNormal))
    {
        return fFalse;
    }
    // 3DMMv1.0: first, set up the root
    pbact = _PbactRoot();
    pbact->type = BR_ACTOR_NONE;
    pbact->t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&pbact->t.t.mat);
    pbact->identifier = (schar *)this; // 3DMMv1.0: to find BODY from a BACT

    // 3DMMv1.0: next, set up hilite actor
    pbact = _PbactHilite();
    pbact->type = BR_ACTOR_NONE;
    pbact->t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&pbact->t.t.mat);
    pbact->identifier = (schar *)this; // 3DMMv1.0: to find BODY from a BACT
    pbact->material = _pbmtlHilite;
    pbact->render_style = BR_RSTYLE_BOUNDING_EDGES;
    BrActorAdd(_PbactRoot(), pbact);

    // 3DMMv1.0: now set up the body part BACTs
    for (ibact = 0; ibact < _cbactPart; ibact++)
    {
        pbact = _PbactPart(ibact);
        pbact->type = BR_ACTOR_MODEL;
        pbact->render_style = BR_RSTYLE_FACES;
        pbact->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&pbact->t.t.mat);
        pbact->identifier = (schar *)this; // 3DMMv1.0: to find BODY from a BACT

        // 3DMMv1.0: Find parent of this body part
        pglibactPar->Get(ibact, &ibactPar);
        if (ivNil == ibactPar)
        {
            pbactPar = _PbactRoot();
        }
        else
        {
            AssertIn(ibactPar, 0, ibact);
            pbactPar = _PbactPart(ibactPar);
        }
        BrActorAdd(pbactPar, pbact);
#if defined(BRENDER_MODERN_14)
        BrModernLog("BODY::_FInitShape actor part=%ld actor=%p parent_index=%ld parent=%p type=%u",
                    (long)ibact, pbact, (long)ibactPar, pbactPar, (unsigned)pbact->type);
#endif
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::_FInitShape SUCCESS this=%p root=%p hilite=%p parts=%ld sets=%ld", this,
                _PbactRoot(), _PbactHilite(), (long)_cbactPart, (long)_cbset);
#endif
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Change the body part hierarchy and/or the body part sets of this BODY.
    Models, materials, and matrices are not changed for body parts that
    exist in both the old and reshaped BODYs.
***************************************************************************/
bool BODY::FChangeShape(PGL pglibactPar, PGL pglibset)
{
    AssertThis(fobjAssertFull);
    AssertPo(pglibactPar, 0);
    AssertPo(pglibset, 0);

    PBODY pbodyDup;
    int32_t ibset;
    bool fMtrl;
    PMTRL pmtrl;
    PCMTL pcmtl;
    int32_t ibact;
    PBACT pbactDup;
    PBACT pbact;
    int32_t cactHidden = _cactHidden;

    pbodyDup = PbodyDup();
    if (pvNil == pbodyDup)
        goto LFail;

    _DestroyShape(); // 3DMMv1.0: note: hides the body
    if (!_FInitShape(pglibactPar, pglibset))
        goto LFail;
    if (cactHidden == 0) // 3DMMv1.0: if body was visible
        Show();
    // 3DMMv1.0: Restore materials
    for (ibset = 0; ibset < LwMin(_cbset, pbodyDup->_cbset); ibset++)
    {
        pbodyDup->GetPartSetMaterial(ibset, &fMtrl, &pmtrl, &pcmtl);
        if (fMtrl)
            SetPartSetMtrl(ibset, pmtrl);
        else
            SetPartSetCmtl(pcmtl);
    }
    // 3DMMv1.0: Restore models and matrices
    for (ibact = 0; ibact < LwMin(_cbactPart, pbodyDup->_cbactPart); ibact++)
    {
        pbactDup = pbodyDup->_PbactPart(ibact);
        pbact = _PbactPart(ibact);
        if (pvNil != pbactDup->model)
            SetPartModel(ibact, MODL::PmodlFromBmdl(pbactDup->model));
        pbact->t.t.mat = pbactDup->t.t.mat;
    }
    _PbactRoot()->t.t.mat = pbodyDup->_PbactRoot()->t.t.mat;
    // _FInitShape creates a fresh BR_ACTOR_NONE root. Reattach the object-wide
    // shadow policy immediately so costume/shape changes cannot temporarily
    // revert Object Properties until the next Light Lab refresh.
    Sync4DMMBodyShadowRoot(this);
    // 3DMMv1.0: Restore hilite state
    if (pbodyDup->_PbactHilite()->type == BR_ACTOR_MODEL)
        Hilite(); // 3DMMv1.0: body was hilited, so hilite it now
    ReleasePpo(&pbodyDup);
    AssertThis(fobjAssertFull);
    return fTrue;
LFail:
    if (pvNil != pbodyDup)
    {
        Restore(pbodyDup);
        if (cactHidden == 0)
            Show();
        ReleasePpo(&pbodyDup);
    }
    AssertThis(fobjAssertFull);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Returns the BODY that owns the given BACT.  Also, if pibset is not
    nil, returns what body part set this BACT is in.  If the pbact is
    the hilite BACT, pibset is set to ivNil.
***************************************************************************/
BODY *BODY::PbodyFromBact(BACT *pbact, int32_t *pibset)
{
    AssertVarMem(pbact);
    AssertNilOrVarMem(pibset);

    PBODY pbody;
    int32_t ibact;

    pbody = (BODY *)pbact->identifier;
    if (pvNil == pbody)
    {
        Bug("What actor is this?  It has no BODY");
        return pvNil;
    }
    AssertPo(pbody, 0);
    if (pvNil != pibset)
    {
        *pibset = ivNil;
        for (ibact = 0; ibact < pbody->_cbactPart; ibact++)
        {
            if (pbact == pbody->_PbactPart(ibact))
            {
                *pibset = pbody->_Ibset(ibact);
                break;
            }
        }
    }
    return pbody;
}

/** 3DMMv1.0: *************************************************************************
    Returns the BODY that is under the given point.  Also, if pibset is not
    nil, returns what body part set this point is in.
***************************************************************************/
struct BODYCLICKFILTERCONTEXT
{
    PFNBODYCLICKFILTER pfnFilter;
    void *pvContext;
};

// BrScenePick2D has already intersected each candidate BRender model's real
// local-space 3D bounding box before _FFilter runs. On small stock actors the
// legacy fixed-point scene-pick ray can still miss every individual triangle,
// even though that same ray clearly passed through a body-part box. Keep the
// nearest box candidate as a conservative second tier and use it only when no
// exact triangle hit exists anywhere under the cursor.
static PBODY vpbody4DMMBoundsClicked = pvNil;
static PBACT vpbact4DMMBoundsClicked = pvNil;
static BRS vdzp4DMMBoundsClicked = BR_SCALAR_MAX;
static int32_t vc4DMMExactPickCandidates = 0;
static int32_t vc4DMMBoundsPickCandidates = 0;
static uint32_t vc4DMMPickMisses = 0;

// Main-viewport click selection should resolve the geometry under the cursor,
// not merely whichever BODY happens to have the nearest intersected bounding
// box. BrScenePick2D already gives _FFilter the candidate model's local-space
// ray and bounding-box interval. The v179 exact-face pass reused BrModelPick2D,
// but 4DMM deliberately builds BRender 1.4 with the legacy 16.16 fixed ABI.
// Its old plane-equation intersection loses enough precision on small models
// that cats/dogs can become orientation- and insertion-order-dependent picks.
// Intersect the actual model triangles in double precision while preserving the
// same local ray parameter, so depth remains comparable between BODY parts.
static bool F4DMMBodyExactModelPick(
    PBMDL pbmdl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir,
    BRS dzpNear, BRS dzpFar, BRS *pdzpHit, int32_t *pifaceHit)
{
    if (pbmdl == pvNil || pbmdl->vertices == pvNil || pbmdl->faces == pvNil ||
        pbmdl->nvertices == 0 || pbmdl->nfaces == 0 || pbvec3RayPos == pvNil ||
        pbvec3RayDir == pvNil || pdzpHit == pvNil || pifaceHit == pvNil)
        return fFalse;

    const double ox = (double)BrScalarToFloat(pbvec3RayPos->v[0]);
    const double oy = (double)BrScalarToFloat(pbvec3RayPos->v[1]);
    const double oz = (double)BrScalarToFloat(pbvec3RayPos->v[2]);
    const double dx = (double)BrScalarToFloat(pbvec3RayDir->v[0]);
    const double dy = (double)BrScalarToFloat(pbvec3RayDir->v[1]);
    const double dz = (double)BrScalarToFloat(pbvec3RayDir->v[2]);
    const double tNear = (double)BrScalarToFloat(dzpNear) - 0.001;
    const double tFar = (double)BrScalarToFloat(dzpFar) + 0.001;
    const double epsDet = 1.0e-12;
    const double epsBary = 1.0e-8;
    bool fHit = fFalse;
    double tClosest = tFar;
    int32_t ifaceClosest = ivNil;

    for (int32_t iface = 0; iface < (int32_t)pbmdl->nfaces; ++iface)
    {
        const br_face &face = pbmdl->faces[iface];
        const int32_t i0 = face.vertices[0];
        const int32_t i1 = face.vertices[1];
        const int32_t i2 = face.vertices[2];
        if (!FIn(i0, 0, (int32_t)pbmdl->nvertices) ||
            !FIn(i1, 0, (int32_t)pbmdl->nvertices) ||
            !FIn(i2, 0, (int32_t)pbmdl->nvertices))
            continue;

        const br_vector3 &p0 = pbmdl->vertices[i0].p;
        const br_vector3 &p1 = pbmdl->vertices[i1].p;
        const br_vector3 &p2 = pbmdl->vertices[i2].p;
        const double v0x = (double)BrScalarToFloat(p0.v[0]);
        const double v0y = (double)BrScalarToFloat(p0.v[1]);
        const double v0z = (double)BrScalarToFloat(p0.v[2]);
        const double e1x = (double)BrScalarToFloat(p1.v[0]) - v0x;
        const double e1y = (double)BrScalarToFloat(p1.v[1]) - v0y;
        const double e1z = (double)BrScalarToFloat(p1.v[2]) - v0z;
        const double e2x = (double)BrScalarToFloat(p2.v[0]) - v0x;
        const double e2y = (double)BrScalarToFloat(p2.v[1]) - v0y;
        const double e2z = (double)BrScalarToFloat(p2.v[2]) - v0z;

        const double px = dy * e2z - dz * e2y;
        const double py = dz * e2x - dx * e2z;
        const double pz = dx * e2y - dy * e2x;
        const double det = e1x * px + e1y * py + e1z * pz;
        if (det > -epsDet && det < epsDet)
            continue;
        const double invDet = 1.0 / det;

        const double tx = ox - v0x;
        const double ty = oy - v0y;
        const double tz = oz - v0z;
        const double u = (tx * px + ty * py + tz * pz) * invDet;
        if (u < -epsBary || u > 1.0 + epsBary)
            continue;

        const double qx = ty * e1z - tz * e1y;
        const double qy = tz * e1x - tx * e1z;
        const double qz = tx * e1y - ty * e1x;
        const double v = (dx * qx + dy * qy + dz * qz) * invDet;
        if (v < -epsBary || u + v > 1.0 + epsBary)
            continue;

        const double t = (e2x * qx + e2y * qy + e2z * qz) * invDet;
        if (t < tNear || t > tFar || t < 0.0 || (fHit && t >= tClosest))
            continue;

        fHit = fTrue;
        tClosest = t;
        ifaceClosest = iface;
    }

    if (!fHit)
        return fFalse;
    *pdzpHit = BrFloatToScalar((float)tClosest);
    *pifaceHit = ifaceClosest;
    return fTrue;
}

BODY *BODY::PbodyClicked(int32_t xp, int32_t yp, PBWLD pbwld, int32_t *pibset,
                         PFNBODYCLICKFILTER pfnFilter, void *pvContext)
{
    AssertNilOrVarMem(pibset);
    AssertPo(pbwld, 0);

    PBODY pbody;
    int32_t ibact;
    BODYCLICKFILTERCONTEXT filterContext = { pfnFilter, pvContext };

    _pbodyClosestClicked = pvNil;
    _pbactClosestClicked = pvNil;
    _dzpClosestClicked = BR_SCALAR_MAX;
    vpbody4DMMBoundsClicked = pvNil;
    vpbact4DMMBoundsClicked = pvNil;
    vdzp4DMMBoundsClicked = BR_SCALAR_MAX;
    vc4DMMExactPickCandidates = 0;
    vc4DMMBoundsPickCandidates = 0;

    pbwld->IterateActorsInPt(BODY::_FFilter, &filterContext, xp, yp);

    const bool fBoundsFallback = _pbodyClosestClicked == pvNil &&
                                 vpbody4DMMBoundsClicked != pvNil;
    if (fBoundsFallback)
    {
        _pbodyClosestClicked = vpbody4DMMBoundsClicked;
        _pbactClosestClicked = vpbact4DMMBoundsClicked;
        _dzpClosestClicked = vdzp4DMMBoundsClicked;
    }

    if (_pbodyClosestClicked != pvNil)
    {
        BrModernLog(
            "main_pick selected mode=%s xy=%ld,%ld body=%p bact=%p depth=%g exact_candidates=%ld bounds_candidates=%ld",
            fBoundsFallback ? "bounds" : "exact", (long)xp, (long)yp,
            _pbodyClosestClicked, _pbactClosestClicked,
            (double)BrScalarToFloat(_dzpClosestClicked),
            (long)vc4DMMExactPickCandidates, (long)vc4DMMBoundsPickCandidates);
        MVIE::MultiLog(pvNil,
            "main_pick selected mode=%s xy=%ld,%ld body=%p bact=%p depth=%g exact_candidates=%ld bounds_candidates=%ld",
            fBoundsFallback ? "bounds" : "exact", (long)xp, (long)yp,
            _pbodyClosestClicked, _pbactClosestClicked,
            (double)BrScalarToFloat(_dzpClosestClicked),
            (long)vc4DMMExactPickCandidates, (long)vc4DMMBoundsPickCandidates);
    }
    else if (((++vc4DMMPickMisses) & 0x3f) == 0)
    {
        // Do not let -multi_log change click timing by writing dozens of lines
        // for every BODY part under every miss. One sampled miss is enough to
        // establish that neither exact geometry nor a BRender box was hit.
        MVIE::MultiLog(pvNil,
            "main_pick miss_sample xy=%ld,%ld exact_candidates=%ld bounds_candidates=%ld total_misses=%lu",
            (long)xp, (long)yp, (long)vc4DMMExactPickCandidates,
            (long)vc4DMMBoundsPickCandidates, (unsigned long)vc4DMMPickMisses);
    }

    pbody = _pbodyClosestClicked;
    if (pvNil == _pbodyClosestClicked)
    {
        return pvNil;
    }
    AssertPo(_pbodyClosestClicked, 0);
    if (pvNil != pibset)
    {
        *pibset = ivNil;
        for (ibact = 0; ibact < _pbodyClosestClicked->_cbactPart; ibact++)
        {
            if (_pbactClosestClicked == _pbodyClosestClicked->_PbactPart(ibact))
            {
                *pibset = _pbodyClosestClicked->_Ibset(ibact);
                break;
            }
        }
    }
    return _pbodyClosestClicked;
}

/** 3DMMv1.0: *************************************************************************
    Filter callback proc for PbodyClicked().  Saves pbody if it's the
    closest one hit so far and visible
***************************************************************************/
int BODY::_FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir, BRS dzpNear,
                   BRS dzpFar, void *pv)
{
    AssertVarMem(pbact);
    AssertVarMem(pbvec3RayPos);
    AssertVarMem(pbvec3RayDir);

    PBODY pbody;

    pbody = BODY::PbodyFromBact(pbact);
    BODYCLICKFILTERCONTEXT *pfilter = (BODYCLICKFILTERCONTEXT *)pv;
    if (pbody != pvNil && pfilter != pvNil && pfilter->pfnFilter != pvNil &&
        !pfilter->pfnFilter(pbody, pfilter->pvContext))
    {
        return 0;
    }

    if (pbody == pvNil || !pbody->FIsInView() || pbact == pbody->_PbactHilite() ||
        pbact->model == pvNil || pbact->model->faces == pvNil ||
        pbact->model->nfaces == 0 || pbmtl == pvNil)
    {
        return fFalse;
    }

    BRS dzpExact = BR_SCALAR_MAX;
    int32_t ifaceExact = ivNil;
    if (F4DMMBodyExactModelPick(pbact->model, pbvec3RayPos, pbvec3RayDir,
                                dzpNear, dzpFar, &dzpExact, &ifaceExact))
    {
        ++vc4DMMExactPickCandidates;
        if (dzpExact < _dzpClosestClicked)
        {
            _pbodyClosestClicked = pbody;
            _dzpClosestClicked = dzpExact;
            _pbactClosestClicked = pbact;
        }
    }
    else
    {
        ++vc4DMMBoundsPickCandidates;
        if (dzpNear < vdzp4DMMBoundsClicked)
        {
            vpbody4DMMBoundsClicked = pbody;
            vpbact4DMMBoundsClicked = pbact;
            vdzp4DMMBoundsClicked = dzpNear;
        }
    }

    return fFalse; // 3DMMv1.0: fFalse means keep searching
}

/** 3DMMv1.0: *************************************************************************
    Destroy the BODY's body parts, including their attached materials and
    models.
***************************************************************************/
void BODY::_DestroyShape(void)
{
    AssertBaseThis(0);
    int32_t ibset;
    int32_t ibact;
    BACT *pbact;
    MODL *pmodl;

    // 3DMMv1.0: Must hide body before destroying it
    if (_cactHidden == 0)
        Hide();

    if (pvNil != _prgbact)
    {
        for (ibact = 0; ibact < _cbactPart; ibact++)
        {
            pbact = _PbactPart(ibact);
            if (pvNil != pbact->model)
            {
                pmodl = MODL::PmodlFromBmdl(pbact->model);
                AssertPo(pmodl, 0);
                ReleasePpo(&pmodl);
                pbact->model = pvNil;
            }
        }
        if (pvNil != _prgpcmtl)
        {
            for (ibset = 0; ibset < _cbset; ibset++)
                _RemoveMaterial(ibset);
        }
        FreePpv((void **)&_prgbact);
    }
    FreePpv((void **)&_prgpcmtl);
    ReleasePpo(&_pglibset);
}

/** 3DMMv1.0: *************************************************************************
    Free the BODY and all attached MODLs, MTRLs, and CMTLs.
***************************************************************************/
BODY::~BODY(void)
{
    AssertBaseThis(0);
    if (vpbody4DMMPartHilite == this)
    {
        vpbody4DMMPartHilite = pvNil;
        vipart4DMMPartHilite = ivNil;
        vcpart4DMMPartHilite = 0;
        vf4DMMPartHiliteTree = fFalse;
    }
    _DestroyShape();
}

/** 3DMMv1.0: *************************************************************************
    Create a duplicate of this BODY.  The duplicate will be hidden
    (_cactHidden == 1) regardless of the state of this BODY.
***************************************************************************/
PBODY BODY::PbodyDup(void)
{
    AssertThis(0);

    PBODY pbodyDup;
    int32_t ibact;
    int32_t ibset;
    PBACT pbact;
    int32_t bv; // 3DMMv1.0: delta in bytes from _prgbact to _pbody->_prgbact
    bool fMtrl;
    PMTRL pmtrl;
    PCMTL pcmtl;
    bool fVis;
    BMAT34 bmat34DupWorld;
    const bool fGrouped = (_P4DMMObjectGroupParent(this) != pvNil);
    if (fGrouped)
        _Get4DMMBodyWorldMatrix(this, &bmat34DupWorld);

    fVis = (_cactHidden == 0);
    if (fVis)
        Hide(); // 3DMMv1.0: temporarily hide this BODY

    pbodyDup = NewObj BODY;
    if (pvNil == pbodyDup)
        goto LFail;

    pbodyDup->_cactHidden = 1;
    pbodyDup->_cbset = _cbset;
    pbodyDup->_cbactPart = _cbactPart;
    pbodyDup->_pbwld = _pbwld;
    pbodyDup->_fFound = _fFound;
    pbodyDup->_ibset = _ibset;
    pbodyDup->_grf4DMMShadowProperties = _grf4DMMShadowProperties;

    if (!FAllocPv((void **)&pbodyDup->_prgbact, LwMul(_Cbact(), SIZEOF(BACT)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    CopyPb(_prgbact, pbodyDup->_prgbact, LwMul(_Cbact(), SIZEOF(BACT)));
    // 3DMMv1.0: need to update BACT parent, child, next, prev pointers
    bv = BvSubPvs(pbodyDup->_prgbact, _prgbact);
    for (ibact = 0; ibact < _Cbact(); ibact++)
    {
        pbact = &pbodyDup->_prgbact[ibact];
        pbact->identifier = (schar *)pbodyDup;
        if (ibact == 0)
        {
            pbact->parent = pvNil;
            if (pvNil != pbact->children)
                pbact->children = (PBACT)PvAddBv(pbact->children, bv);
            pbact->next = pvNil;
            pbact->prev = pvNil;
            // A BODY duplicate is an independent snapshot/copy, never a
            // runtime child of the source Object Group parent. Preserve its
            // apparent world pose and clear the transient parent cookie.
            if (fGrouped)
                BrMatrix34Copy(&pbact->t.t.mat, &bmat34DupWorld);
            pbact->user = pvNil;
            pbact->type_data = pbodyDup->_grf4DMMShadowProperties != 0 ?
                (void *)&pbodyDup->_grf4DMMShadowProperties : pvNil;
        }
        else
        {
            if (pvNil != pbact->parent)
                pbact->parent = (PBACT)PvAddBv(pbact->parent, bv);
            if (pvNil != pbact->children)
                pbact->children = (PBACT)PvAddBv(pbact->children, bv);
            if (pvNil != pbact->next)
                pbact->next = (PBACT)PvAddBv(pbact->next, bv);
            if (pvNil != pbact->prev)
                pbact->prev = (PBACT *)PvAddBv(pbact->prev, bv);
        }
    }
    for (ibact = 0; ibact < pbodyDup->_cbactPart; ibact++)
    {
        pbact = _PbactPart(ibact);
        if (pvNil != pbact->model)
            MODL::PmodlFromBmdl(pbact->model)->AddRef();
    }

    pbodyDup->_pglibset = _pglibset->PglDup();
    if (pvNil == pbodyDup->_pglibset)
        goto LFail;

    if (!FAllocPv((void **)&pbodyDup->_prgpcmtl, LwMul(_cbset, SIZEOF(PCMTL)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    CopyPb(_prgpcmtl, pbodyDup->_prgpcmtl, LwMul(_cbset, SIZEOF(PCMTL)));

    pbodyDup->_rcBounds = _rcBounds;
    pbodyDup->_rcBoundsLastVis = _rcBoundsLastVis;

    for (ibset = 0; ibset < _cbset; ibset++)
    {
        pbodyDup->GetPartSetMaterial(ibset, &fMtrl, &pmtrl, &pcmtl);
        if (fMtrl)
        {
            // 3DMMv1.0: need to AddRef once per body part in this set
            for (ibact = 0; ibact < pbodyDup->_cbactPart; ibact++)
            {
                if (ibset == _Ibset(ibact))
                    pmtrl->AddRef();
            }
        }
        else
        {
            pcmtl->AddRef();
        }
    }
    if (fVis)
        Show(); // 3DMMv1.0: re-show this BODY (but not the duplicate)

    AssertPo(pbodyDup, 0);
    return pbodyDup;
LFail:
    if (fVis)
        Show();
    ReleasePpo(&pbodyDup);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Replace this BODY with pbodyDup.  Preserve this BODY's hidden-ness.
***************************************************************************/
void BODY::Restore(PBODY pbodyDup)
{
    AssertBaseThis(0);
    AssertPo(pbodyDup, 0);
    Assert(pbodyDup->_cactHidden == 1, "dup hidden count must be 1");

    int32_t cactHidden = _cactHidden;
    int32_t ibact;
    PBACT pbactGroupParent = _P4DMMObjectGroupParent(this);

    SwapVars(this, pbodyDup);
    for (ibact = 0; ibact < _Cbact(); ibact++)
        _prgbact[ibact].identifier = (schar *)this;
    Sync4DMMBodyShadowRoot(this);
    Sync4DMMBodyShadowRoot(pbodyDup);

    if (pbactGroupParent != pvNil)
    {
        // PbodyDup stores a grouped snapshot in world space. Convert that
        // replacement body back to the same parent's local space before Show.
        BMAT34 bmat34ParentInv;
        if (BrMatrix34Inverse(&bmat34ParentInv, &pbactGroupParent->t.t.mat) != rZero)
        {
            BMAT34 bmat34World;
            BrMatrix34Copy(&bmat34World, &_PbactRoot()->t.t.mat);
            BrMatrix34Mul(&_PbactRoot()->t.t.mat, &bmat34World, &bmat34ParentInv);
            _PbactRoot()->user = pbactGroupParent;
        }
        else
            _PbactRoot()->user = pvNil;

        // The swapped-out snapshot must not retain a runtime parent cookie.
        pbodyDup->_PbactRoot()->user = pvNil;
    }

    if (cactHidden == 0)
        Show();
    _cactHidden = cactHidden;

    AssertThis(fobjAssertFull);
}

/** 3DMMv1.0: *************************************************************************
    Make this BODY visible, if it was invisible.  Keeps a refcount.
***************************************************************************/
void BODY::Show(void)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::Show BEGIN this=%p hidden=%ld bwld=%p root=%p", this, (long)_cactHidden, _pbwld, _PbactRoot());
#endif
    AssertThis(0);
    Assert(_cactHidden > 0, "object is already visible!");
    if (--_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        PBACT pbactGroupParent = _P4DMMObjectGroupParent(this);
        if (pbactGroupParent != pvNil && pbactGroupParent->prev != pvNil)
            BrActorAdd(pbactGroupParent, _PbactRoot());
        else
        {
            // A stale runtime parent must never strand a BODY outside BWLD.
            if (pbactGroupParent != pvNil)
                _PbactRoot()->user = pvNil;
            _pbwld->AddActor(_PbactRoot());
        }
        _pbwld->SetBeginRenderCallback(_PrepareToRender);
        _pbwld->SetActorRenderedCallback(_BactRendered);
        _pbwld->SetGetRcCallback(_GetRc);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
#if defined(BRENDER_MODERN_14)
        BrModernLog("BODY::Show attached this=%p root=%p world=%p", this, _PbactRoot(), _pbwld);
#endif
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::Show END this=%p hidden=%ld", this, (long)_cactHidden);
#endif
}

/** 3DMMv1.0: *************************************************************************
    Make this BODY invisible, if it was visible.  Keeps a refcount.
***************************************************************************/
void BODY::Hide(void)
{
    AssertThis(0);

    RC rc;

    if (_cactHidden++ == 0)
    {
        _rcBounds.Zero();
        BrActorRemove(_PbactRoot());
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/** 3DMMv1.0: *************************************************************************
    Sets the hilite color to use.
***************************************************************************/
void BODY::SetHiliteColor(int32_t iclr)
{
    if (_pbmtlHilite != pvNil)
    {
        // In the 4DMM RGB888 selection modes the visible colour is explicit
        // RGB, so do not let the old time-freeze/indexed selection bookkeeping
        // overwrite the authored yellow fallback index.
        if (_F4dmmHiliteModesActive())
            _pbmtlHilite->index_base = kiclrNormalHilite;
        else
            _pbmtlHilite->index_base = (uint8_t)iclr;
    }
}

/***************************************************************************
    Return the active 4DMM selection-box presentation mode.
***************************************************************************/
int32_t BODY::ImodHilite(void)
{
    return _imod4dmmHilite;
}

/***************************************************************************
    Advance the 4DMM selection-box presentation mode.  The exact per-pixel
    inverse-overlay mode needs a post-raster/XOR-style pass rather than a
    BRender material, so this first toggle cycles the three modes that can be
    expressed faithfully by the existing bounding-edge actor.
***************************************************************************/
int32_t BODY::CycleHiliteMode(void)
{
    if (!_F4dmmHiliteModesActive())
        return _imod4dmmHilite;

    _imod4dmmHilite = (_imod4dmmHilite + 1) % 3;
    _Apply4dmmHiliteMaterial(_pbmtlHilite);
    return _imod4dmmHilite;
}

/** 3DMMv1.0: *************************************************************************
    Hilites the BODY
***************************************************************************/
void BODY::Hilite(void)
{
    AssertThis(0);

    PBMTL pbmtl = _pbmtlHilite;
    if (vf4DMMGroupedHiliteNext)
    {
        PBMTL pbmtlGrouped = Pbmtl4DMMGroupedHilite();
        if (pbmtlGrouped != pvNil)
            pbmtl = pbmtlGrouped;
    }
    vf4DMMGroupedHiliteNext = fFalse;
    _PbactHilite()->material = pbmtl;
    _PbactHilite()->type = BR_ACTOR_MODEL;
    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/** 3DMMv1.0: *************************************************************************
    Unhilites the BODY
***************************************************************************/
void BODY::Unhilite(void)
{
    AssertThis(0);

    _PbactHilite()->type = BR_ACTOR_NONE;
    if (vpbody4DMMPartHilite == this)
    {
        vpbody4DMMPartHilite = pvNil;
        vipart4DMMPartHilite = ivNil;
        vcpart4DMMPartHilite = 0;
        vf4DMMPartHiliteTree = fFalse;
    }
    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/***************************************************************************
    Highlight one real BODY part for Actor Studio. The ordinary selection box
    BACT is reused, but _PrepareToRender sizes/positions it from this part's
    model bounds instead of the complete BODY bounds.
***************************************************************************/
void BODY::HilitePart(int32_t ipart)
{
    HilitePartRange(ipart, 1);
}

void BODY::HilitePartRange(int32_t ipartFirst, int32_t cpart)
{
    AssertThis(0);
    AssertIn(ipartFirst, 0, _cbactPart);
    AssertIn(cpart, 1, _cbactPart - ipartFirst + 1);

    vpbody4DMMPartHilite = this;
    vipart4DMMPartHilite = ipartFirst;
    vcpart4DMMPartHilite = cpart;
    vf4DMMPartHiliteTree = fFalse;
    // Actor Studio part/range selection uses the normal yellow selection
    // material, not Object Groups' magenta membership material.
    vf4DMMGroupedHiliteNext = fFalse;
    Hilite();
}

void BODY::HilitePartTree(int32_t ipart)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);

    vpbody4DMMPartHilite = this;
    vipart4DMMPartHilite = ipart;
    vcpart4DMMPartHilite = 1;
    vf4DMMPartHiliteTree = fTrue;
    vf4DMMGroupedHiliteNext = fFalse;
    Hilite();
}

void BODY::ClearPartHilite(void)
{
    AssertThis(0);
    if (vpbody4DMMPartHilite == this)
    {
        vpbody4DMMPartHilite = pvNil;
        vipart4DMMPartHilite = ivNil;
        vcpart4DMMPartHilite = 0;
        vf4DMMPartHiliteTree = fFalse;
    }
    _PbactHilite()->type = BR_ACTOR_NONE;
    if (_cactHidden == 0 && _pbwld != pvNil)
        _pbwld->MarkDirty();
}

/** 3DMMv1.0: *************************************************************************
    Position the body at (xr, yr, zr) in worldspace	oriented by pbmat34
***************************************************************************/
void BODY::LocateOrient(BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertVarMem(pbmat34);

    BMAT34 bmat34World;
    BrMatrix34Copy(&bmat34World, pbmat34);
    BrMatrix34PostTranslate(&bmat34World, xr, yr, zr);

    PBACT pbactGroupParent = _P4DMMObjectGroupParent(this);
    if (pbactGroupParent != pvNil)
    {
        BMAT34 bmat34ParentInv;
        if (BrMatrix34Inverse(&bmat34ParentInv, &pbactGroupParent->t.t.mat) != rZero)
            BrMatrix34Mul(&_PbactRoot()->t.t.mat, &bmat34World, &bmat34ParentInv);
        else
            BrMatrix34Copy(&_PbactRoot()->t.t.mat, &bmat34World);
    }
    else
        BrMatrix34Copy(&_PbactRoot()->t.t.mat, &bmat34World);
    _PbactRoot()->t.type = BR_TRANSFORM_MATRIX34;

    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/***************************************************************************
    Remove only the renderable model from one BODY node while preserving its
    hierarchy, matrix, body-set/material assignment, and descendants. Actor
    Studio Cut uses this for temporal absence; permanent Delete still removes
    topology through the existing template mutation path.
***************************************************************************/
void BODY::ClearPartModel(int32_t ibact)
{
    AssertThis(0);
    AssertIn(ibact, 0, _cbactPart);

    PBACT pbact = _PbactPart(ibact);
    if (pbact->model == pvNil)
        return;

    PMODL pmodlOld = MODL::PmodlFromBmdl(pbact->model);
    AssertPo(pmodlOld, 0);
    pbact->model = pvNil;
    // A BR_ACTOR_MODEL with a null model does not mean "draw nothing" in
    // BRender: it falls back to BRender's built-in default model.  That is the
    // cube/play-dough/cylinder-looking impostor seen after Actor Studio Cut.
    // Temporally absent BODY nodes must remain in the hierarchy for children,
    // but the actor itself must be non-rendering until Spawn restores a model.
    pbact->type = BR_ACTOR_NONE;
    ReleasePpo(&pmodlOld);

    if (_cactHidden == 0 && _pbwld != pvNil)
        _pbwld->MarkDirty();

#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::ClearPartModel part=%ld actor=%p", (long)ibact, pbact);
#endif
}

/** 3DMMv1.0: *************************************************************************
    Set the ibact'th body part to use model pmodl
***************************************************************************/
void BODY::SetPartModel(int32_t ibact, MODL *pmodl)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::SetPartModel BEGIN this=%p part=%ld incoming_modl=%p incoming_bmdl=%p current_bmdl=%p",
                this, (long)ibact, pmodl, pmodl != pvNil ? pmodl->Pbmdl() : pvNil,
                (ibact >= 0 && ibact < _cbactPart) ? _PbactPart(ibact)->model : pvNil);
#endif
    uint64_t qwPerfPartModel = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    AssertThis(0);
    AssertIn(ibact, 0, _cbactPart);
    AssertPo(pmodl, 0);

    BACT *pbact = _PbactPart(ibact);
    PMODL pmodlOld;
    bool fSameSource = fFalse;
    bool fSameResource = fFalse;
    bool fSameFile = fFalse;
    bool fFileReuse = fFalse;
    bool fSameContent = fFalse;
    bool fContentReuse = fFalse;
    uint32_t cusecContentCompare = 0;
    bool fSameGeometry = fFalse;
    bool fGeometryReuse = fFalse;
    uint32_t cusecGeometryCompare = 0;
    int32_t iGeometryReject = kgeomrejNil;

    if (pvNil != pbact->model) // 3DMMv1.0: Release old MODL, unless it's pmodl
    {
        pmodlOld = MODL::PmodlFromBmdl(pbact->model);
        AssertPo(pmodlOld, 0);
        fSameSource = pmodlOld->Ctg() == pmodl->Ctg() && pmodlOld->Cno() == pmodl->Cno();
        // actorlight21: exact CRF identity was the first safe resource test.
        // Generated MODLs have no CRF and their BACO ctg/cno fields are not a
        // safe identity by themselves. actorlight23 broadens this below only
        // when both CRFs wrap the exact same underlying chunky file.
        fSameResource = pmodlOld->Pcrf() != pvNil && pmodlOld->Pcrf() == pmodl->Pcrf() && fSameSource;
        // actorlight23: CRF is only the cache wrapper. Two different CRFs can
        // legitimately wrap the same underlying chunky file. PCFL + CTG + CNO
        // identifies the exact same immutable source chunk without conflating
        // coincident chunk numbers from different content files.
        fSameFile = pmodlOld->Pcrf() != pvNil && pmodl->Pcrf() != pvNil &&
                    pmodlOld->Pcrf()->Pcfl() == pmodl->Pcrf()->Pcfl() && fSameSource;
        if (pmodl == pmodlOld)
        {
            if (MVIE::FPerformanceMode())
                MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fTrue, fSameSource,
                                           fSameResource, fFalse, fSameFile, fFalse,
                                           fFalse, fFalse, 0, fFalse, fFalse, 0, kgeomrejNil);
            return; // 3DMMv1.0: We're already using that MODL, so do nothing
        }
        if (fSameResource)
        {
            // The actor already owns an instance of this exact immutable source
            // chunk. Keep that actor-local instance instead of swapping in a
            // second wrapper for identical geometry. This avoids destroying and
            // re-preparing models while preserving per-actor BRender state.
            if (MVIE::FPerformanceMode())
                MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                           fTrue, fTrue, fSameFile, fFalse,
                                           fFalse, fFalse, 0, fFalse, fFalse, 0, kgeomrejNil);
            return;
        }
        if (fSameFile)
        {
            // Keep the actor's already-prepared local BRender model. The incoming
            // MODL is merely another cache-wrapper instance of the same chunk.
            // This is the v23 fast path intended to eliminate the 636/frame
            // destroy/rebuild cycle without sharing BRender state between actors.
            fFileReuse = fTrue;
            if (MVIE::FPerformanceMode())
                MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                           fFalse, fFalse, fTrue, fTrue,
                                           fFalse, fFalse, 0, fFalse, fFalse, 0, kgeomrejNil);
            return;
        }

        // actorlight25: v24's content/geometry diagnostics never ran for the
        // 636 pathological swaps because both tests were gated on live CRF
        // pointers. A BACO can outlive its CRF; CRF destruction deliberately
        // clears BACO::_pcrf while the MODL and its immutable source fingerprint
        // remain valid. Compare those fingerprints directly. Generated/imported
        // models never receive a source fingerprint, so they cannot enter this
        // reuse path accidentally.
        if (fSameSource)
        {
            uint64_t qwPerfContent = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
            int32_t cbOld = 0;
            int32_t cbNew = 0;
            uint64_t qwOldA = 0;
            uint64_t qwOldB = 0;
            uint64_t qwNewA = 0;
            uint64_t qwNewB = 0;
            bool fOldFingerprint = pmodlOld->FGetSourceFingerprint(&cbOld, &qwOldA, &qwOldB);
            bool fNewFingerprint = pmodl->FGetSourceFingerprint(&cbNew, &qwNewA, &qwNewB);
            bool fSizeMatch = fOldFingerprint && fNewFingerprint && cbOld == cbNew;
            bool fHashAMatch = fSizeMatch && qwOldA == qwNewA;
            bool fHashBMatch = fSizeMatch && qwOldB == qwNewB;

            if (MVIE::FPerformanceMode())
            {
                cusecContentCompare = MVIE::PerfElapsedUs(qwPerfContent);
                MVIE::PerfRecordPartModelProvenance(pmodlOld->Pcrf() != pvNil, pmodl->Pcrf() != pvNil,
                                                     fOldFingerprint, fNewFingerprint, fSizeMatch,
                                                     fHashAMatch, fHashBMatch);
            }

            if (fHashAMatch && fHashBMatch &&
                pmodlOld->Pbmdl() != pvNil && pmodl->Pbmdl() != pvNil &&
                pmodlOld->Pbmdl()->user == pvNil && pmodl->Pbmdl()->user == pvNil &&
                pmodlOld->Pbmdl()->custom == pvNil && pmodl->Pbmdl()->custom == pvNil)
            {
                fSameContent = fTrue;
                fContentReuse = fTrue;
            }

            if (fContentReuse)
            {
                if (MVIE::FPerformanceMode())
                    MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                               fFalse, fFalse, fSameFile, fFileReuse,
                                               fTrue, fTrue, cusecContentCompare,
                                               fFalse, fFalse, 0, kgeomrejNil);
                return;
            }
        }

        // actorlight26: v25 proved that all 636 pathological swaps arrive with
        // both CRF pointers already detached and with no source fingerprint, so
        // the old v22 CRF gate prevented the prepared-geometry test from ever
        // running.  Geometry identity does not require a live CRF: the BMDLs are
        // still valid and the comparator independently rejects custom/user models,
        // incompatible layouts, materials, bounds, topology, normals, UVs and
        // prelight state.  Run the exact comparison for every same-CTG/CNO swap.
        if (fSameSource)
        {
            uint64_t qwPerfGeometry = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
            fSameGeometry = _FSamePreparedModelGeometry(pmodlOld->Pbmdl(), pmodl->Pbmdl(), &iGeometryReject);
            if (MVIE::FPerformanceMode())
                cusecGeometryCompare = MVIE::PerfElapsedUs(qwPerfGeometry);
            if (fSameGeometry)
            {
                fGeometryReuse = fTrue;
                if (MVIE::FPerformanceMode())
                    MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                               fFalse, fFalse, fSameFile, fFileReuse,
                                               fSameContent, fContentReuse, cusecContentCompare,
                                               fTrue, fTrue, cusecGeometryCompare, iGeometryReject);
                return;
            }
        }
        ReleasePpo(&pmodlOld);
    }

    pbact->model = pmodl->Pbmdl();
    // Spawn/action reload may be restoring a part that Cut temporarily changed
    // to BR_ACTOR_NONE.  Re-establish the normal BODY-part actor type whenever
    // a real model is installed.
    pbact->type = BR_ACTOR_MODEL;
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::SetPartModel assigned part=%ld actor=%p model=%p identifier=%p verts=%p faces=%p prepared=%p stored=%p",
                (long)ibact, pbact, pbact->model,
                pbact->model != pvNil ? pbact->model->identifier : pvNil,
                pbact->model != pvNil ? pbact->model->vertices : pvNil,
                pbact->model != pvNil ? pbact->model->faces : pvNil,
                pbact->model != pvNil ? pbact->model->prepared : pvNil,
                pbact->model != pvNil ? pbact->model->stored : pvNil);
#endif
    Assert(MODL::PmodlFromBmdl(pbact->model) == pmodl, "MODL problem");
    pmodl->AddRef();
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::SetPartModel AddRef complete part=%ld model=%p", (long)ibact, pbact->model);
#endif

    PMODL pmodlTaxi = _PmodlTaxiPerspectiveSubdivided(pbact->model);
    if (pvNil != pmodlTaxi)
    {
        SetPartModel(ibact, pmodlTaxi);
        ReleasePpo(&pmodlTaxi);

        pbact = _PbactPart(ibact);
        if (MODL::FUvDumpEnabled() && pbact->material != pvNil)
            MODL::DumpBodyPart(pbact->model, pbact->material, this, ibact, _Ibset(ibact));

        if (_cactHidden == 0)
        {
            AssertPo(_pbwld, 0);
            _pbwld->MarkDirty();
        }
        if (MVIE::FPerformanceMode())
            MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                       fSameResource, fFalse, fSameFile, fFileReuse,
                                       fSameContent, fContentReuse, cusecContentCompare,
                                       fSameGeometry, fGeometryReuse, cusecGeometryCompare, iGeometryReject);
        return;
    }

    if (MODL::FUvDumpEnabled() && pbact->material != pvNil &&
        pbact->model->user != &_bTaxiPerspectiveSubdividedModel)
    {
        MODL::DumpBodyPart(pbact->model, pbact->material, this, ibact, _Ibset(ibact));
    }

    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
    if (MVIE::FPerformanceMode())
        MVIE::PerfRecordPartModel(MVIE::PerfElapsedUs(qwPerfPartModel), fFalse, fSameSource,
                                       fSameResource, fFalse, fSameFile, fFileReuse,
                                       fSameContent, fContentReuse, cusecContentCompare,
                                       fSameGeometry, fGeometryReuse, cusecGeometryCompare, iGeometryReject);
}

/***************************************************************************
    Actor Studio read-only part topology helpers.
***************************************************************************/
int32_t BODY::IpartFromBact(PBACT pbact)
{
    AssertThis(0);
    if (pbact == pvNil)
        return ivNil;
    for (int32_t ipart = 0; ipart < _cbactPart; ++ipart)
    {
        if (pbact == _PbactPart(ipart))
            return ipart;
    }
    return ivNil;
}

int32_t BODY::IpartParent(int32_t ipart)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);
    PBACT pbactParent = _PbactPart(ipart)->parent;
    if (pbactParent == _PbactRoot())
        return ivNil;
    for (int32_t ipartParent = 0; ipartParent < _cbactPart; ++ipartParent)
    {
        if (pbactParent == _PbactPart(ipartParent))
            return ipartParent;
    }
    return ivNil;
}

int32_t BODY::IbsetOfPart(int32_t ipart)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);
    return _Ibset(ipart);
}

void BODY::GetPartMatrix(int32_t ipart, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);
    AssertVarMem(pbmat34);
    *pbmat34 = _PbactPart(ipart)->t.t.mat;
}

bool BODY::FGetPartModelBounds(int32_t ipart, BRB *pbrb)
{
    AssertThis(0);
    AssertVarMem(pbrb);
    if (!FIn(ipart, 0, _cbactPart))
        return fFalse;
    PBACT pbact = _PbactPart(ipart);
    if (pbact == pvNil || pbact->model == pvNil)
        return fFalse;
    *pbrb = pbact->model->bounds;
    return fTrue;
}

/***************************************************************************
    Actor Studio structural-part classifier. Classic 3DMM BODY hierarchies
    legitimately contain 0-vertex/0-face BRender model actors whose only job
    is to carry transforms for children. Modern BRender already recognizes
    that exact 0/0 contract as a structural placeholder. Expose only the two
    immutable geometry counts so Actor Studio can distinguish renderable
    parts from those hierarchy nodes without peeking into BODY internals.
***************************************************************************/
bool BODY::FGetPartModelGeometry(int32_t ipart, int32_t *pcver, int32_t *pcfac)
{
    AssertThis(0);
    if (pcver != pvNil)
        *pcver = 0;
    if (pcfac != pvNil)
        *pcfac = 0;
    if (!FIn(ipart, 0, _cbactPart))
        return fFalse;
    PBACT pbact = _PbactPart(ipart);
    if (pbact == pvNil || pbact->model == pvNil)
        return fFalse;
    if (pcver != pvNil)
        *pcver = (int32_t)pbact->model->nvertices;
    if (pcfac != pvNil)
        *pcfac = (int32_t)pbact->model->nfaces;
    return fTrue;
}

// Create Part used BrActorToActorMatrix34 here originally. That routine is
// perfectly appropriate for ordinary runtime transforms, but it concatenates
// 3DMM's fixed-point matrices into another fixed-point matrix at every level.
// Flattening a deep/multi-object hierarchy for permanent Actor Studio storage
// then bakes those intermediate rounding errors into the asset as visible
// object-position drift. Compose the already-authored local matrices in double
// precision and quantize only the final 3x4 result. BODY parts, BODY root and
// the 4DMM Object Group parent are all matrix transforms on this path.
static bool F4DMMActorToAncestorMatrix34HighPrecision(PBACT pbact, PBACT pbactAncestor,
                                                       BMAT34 *pbmat34)
{
    if (pbact == pvNil || pbactAncestor == pvNil || pbmat34 == pvNil)
        return fFalse;

    double rgdAcc[4][3] = {
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 0.0}};

    PBACT pbactCur = pbact;
    while (pbactCur != pbactAncestor)
    {
        if (pbactCur == pvNil)
            return fFalse;

        BMAT34 bmat34Local;
        BrMatrix34Transform(&bmat34Local, &pbactCur->t);
        double rgdLocal[4][3];
        for (int32_t i = 0; i < 4; ++i)
            for (int32_t j = 0; j < 3; ++j)
                rgdLocal[i][j] = (double)BrScalarToFloat(bmat34Local.m[i][j]);

        double rgdNext[4][3];
        for (int32_t i = 0; i < 3; ++i)
            for (int32_t j = 0; j < 3; ++j)
                rgdNext[i][j] = rgdAcc[i][0] * rgdLocal[0][j] +
                                rgdAcc[i][1] * rgdLocal[1][j] +
                                rgdAcc[i][2] * rgdLocal[2][j];
        for (int32_t j = 0; j < 3; ++j)
            rgdNext[3][j] = rgdAcc[3][0] * rgdLocal[0][j] +
                            rgdAcc[3][1] * rgdLocal[1][j] +
                            rgdAcc[3][2] * rgdLocal[2][j] + rgdLocal[3][j];
        CopyPb(rgdNext, rgdAcc, SIZEOF(rgdAcc));
        pbactCur = pbactCur->parent;
    }

    for (int32_t i = 0; i < 4; ++i)
        for (int32_t j = 0; j < 3; ++j)
            pbmat34->m[i][j] = BrFloatToScalar((float)rgdAcc[i][j]);
    return fTrue;
}

/***************************************************************************
    Snapshot a BODY node's full transform relative to a caller-supplied
    ancestor. Unlike FGetPartBakeData, this intentionally works for model-less
    hierarchy/group nodes. Those nodes are real authored BODY transforms and
    must survive handmade Actor/Prop synthesis or their descendants lose the
    original rig hierarchy.
***************************************************************************/
bool BODY::FGetPartBakeTransform(int32_t ipart, PBACT pbactAncestor, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);
    AssertVarMem(pbmat34);

    if (!FIn(ipart, 0, _cbactPart) || pbmat34 == pvNil)
        return fFalse;
    if (pbactAncestor == pvNil)
        pbactAncestor = _PbactRoot();

    PBACT pbact = _PbactPart(ipart);
    if (pbact == pvNil)
        return fFalse;

    // The visible BODY pose is authored and evaluated by BRender itself,
    // including the per-cel rotation matrices used by frozen Actions. Keep the
    // renderer's authoritative fixed-point 3x3 block while preserving the
    // high-precision translation path that avoids permanent hierarchy drift.
    BMAT34 bmat34HighPrecision;
    if (!F4DMMActorToAncestorMatrix34HighPrecision(pbact, pbactAncestor, &bmat34HighPrecision))
        return fFalse;

    BMAT34 bmat34Visible;
    BrActorToActorMatrix34(&bmat34Visible, pbact, pbactAncestor);
    *pbmat34 = bmat34Visible;
    pbmat34->m[3][0] = bmat34HighPrecision.m[3][0];
    pbmat34->m[3][1] = bmat34HighPrecision.m[3][1];
    pbmat34->m[3][2] = bmat34HighPrecision.m[3][2];
    return fTrue;
}

/***************************************************************************
    Snapshot the exact visible model/material and full transform for one BODY
    part relative to a caller-supplied ancestor. Model-less hierarchy nodes are
    represented by FGetPartBakeTransform and intentionally return false here.
***************************************************************************/
bool BODY::FGetPartBakeData(int32_t ipart, PBACT pbactAncestor, PMODL *ppmodl,
                            PMTRL *ppmtrl, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertIn(ipart, 0, _cbactPart);
    AssertVarMem(ppmodl);
    AssertVarMem(ppmtrl);
    AssertVarMem(pbmat34);

    *ppmodl = pvNil;
    *ppmtrl = pvNil;

    PBACT pbact = _PbactPart(ipart);
    if (pbact == pvNil || pbact->model == pvNil || pbact->material == pvNil)
        return fFalse;

    PMODL pmodl = MODL::PmodlFromBmdl(pbact->model);
    PMTRL pmtrl = MTRL::PmtrlFromBmtl(pbact->material);
    if (pmodl == pvNil || pmtrl == pvNil)
        return fFalse;
    if (!FGetPartBakeTransform(ipart, pbactAncestor, pbmat34))
        return fFalse;

    pmodl->AddRef();
    pmtrl->AddRef();
    *ppmodl = pmodl;
    *ppmtrl = pmtrl;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Set the ibact'th body part to use matrix pbmat34
***************************************************************************/
void BODY::SetPartMatrix(int32_t ibact, BMAT34 *pbmat34)
{
    uint64_t qwPerfPartMatrix = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    AssertThis(0);
    AssertIn(ibact, 0, _cbactPart);
    AssertVarMem(pbmat34);

    _PbactPart(ibact)->t.t.mat = *pbmat34;
    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
    if (MVIE::FPerformanceMode())
        MVIE::PerfRecordPartMatrix(MVIE::PerfElapsedUs(qwPerfPartMatrix));
}

/** 3DMMv1.0: *************************************************************************
    Remove old MTRL or CMTL from ibset.  This is nontrivial because there
    could either be a CMTL attached to the bset (in which case we just free
    the CMTL) or a bunch of MTRLs (in which case we free each MTRL).
    Actually, in the latter case, it would be a bunch of copies of the same
    MTRL, but we keep one reference per body part in the set.
***************************************************************************/
void BODY::_RemoveMaterial(int32_t ibset)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);

    PCMTL pcmtlOld;
    BACT *pbact;
    int32_t ibact;
    bool fCmtl = fFalse;

    pcmtlOld = _prgpcmtl[ibset];
    if (pvNil != pcmtlOld) // 3DMMv1.0: there was a CMTL on this bset
    {
        fCmtl = fTrue;
        AssertPo(pcmtlOld, 0);
        ReleasePpo(&pcmtlOld); // 3DMMv1.0: free all old MTRLs
        _prgpcmtl[ibset] = pvNil;
    }
    // 3DMMv1.0: for each body part, if this part is in the set ibset, free the MTRL
    for (ibact = 0; ibact < _cbactPart; ibact++)
    {
        if (ibset == _Ibset(ibact))
        {
            pbact = _PbactPart(ibact);
            if (pbact->material != pvNil) // 3DMMv1.0: free old MTRL
            {
                if (!fCmtl)
                    MTRL::PmtrlFromBmtl(pbact->material)->Release();
                pbact->material = pvNil;
            }
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Set the ibset'th body part set to use material pmtrl
***************************************************************************/
void BODY::SetPartSetMtrl(int32_t ibset, MTRL *pmtrl)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);
    AssertPo(pmtrl, 0);

    BACT *pbact;
    int32_t ibact;

    _RemoveMaterial(ibset); // 3DMMv1.0: remove existing MTRL/CMTL, if any

    // 3DMMv1.0: for each body part, if this part is in the set ibset, set the MTRL
    for (ibact = 0; ibact < _cbactPart; ibact++)
    {
        if (ibset == _Ibset(ibact))
        {
            pbact = _PbactPart(ibact);
            pbact->material = pmtrl->Pbmtl();
            pmtrl->AddRef();
            if (MODL::FUvDumpEnabled() && pbact->model != pvNil)
                MODL::DumpBodyPart(pbact->model, pbact->material, this, ibact, ibset);
        }
    }
    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/** 3DMMv1.0: *************************************************************************
    Apply the given CMTL to the appropriate body part set (the CMTL knows
    which body part set to apply to).
***************************************************************************/
void BODY::SetPartSetCmtl(CMTL *pcmtl)
{
    AssertThis(0);
    AssertPo(pcmtl, 0);

    BACT *pbact;
    int32_t ibact;
    int32_t ibmtl = 0;
    int32_t ibset = pcmtl->Ibset();
    PMODL pmodl;

    pcmtl->AddRef();
    _RemoveMaterial(ibset); // 3DMMv1.0: remove existing MTRL/CMTL
    _prgpcmtl[ibset] = pcmtl;

    // 3DMMv1.0: for each body part, if this part is in the set ibset, set the MTRL
    for (ibact = 0; ibact < _cbactPart; ibact++)
    {
        if (ibset == _Ibset(ibact))
        {
            pbact = _PbactPart(ibact);
            // 3DMMv1.0: Handle model changes for accessories
            pmodl = pcmtl->Pmodl(ibmtl);
            if (pvNil != pmodl)
                SetPartModel(ibact, pmodl);
            pbact->material = pcmtl->Pbmtl(ibmtl);
            if (MODL::FUvDumpEnabled() && pbact->model != pvNil && pbact->material != pvNil)
                MODL::DumpBodyPart(pbact->model, pbact->material, this, ibact, ibset);
            ibmtl++;
        }
    }
#if !defined(IGNORE_UNUSED_CUSTOM_MATERIALS)
    Assert(ibmtl == pcmtl->Cbprt(), "didn't use all custom materials!");
#endif
    if (_cactHidden == 0)
    {
        AssertPo(_pbwld, 0);
        _pbwld->MarkDirty(); // 3DMMv1.0: need to render
    }
}

/** 3DMMv1.0: *************************************************************************
    Determines the current CMTL or MTRL applied to the given ibset.
    If a CMTL is attached, *pfMtrl is fFalse and *ppcmtl holds the
    PCMTL.  If a MTRL is attached, *pfMtrl is fTrue and *ppmtrl holds
    the PMTRL.
***************************************************************************/
void BODY::GetPartSetMaterial(int32_t ibset, bool *pfMtrl, MTRL **ppmtrl, CMTL **ppcmtl)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);
    AssertVarMem(pfMtrl);
    AssertVarMem(ppmtrl);
    AssertVarMem(ppcmtl);

    BACT *pbact;
    int32_t ibact;

    *ppcmtl = _prgpcmtl[ibset];
    if (pvNil != *ppcmtl) // 3DMMv1.0: there is a CMTL on this bset
    {
        *pfMtrl = fFalse;
        AssertPo(*ppcmtl, 0);
        TrashVar(ppmtrl);
    }
    else
    {
        *pfMtrl = fTrue;
        TrashVar(ppcmtl);
        // 3DMMv1.0: Find any body part of ibset...they'll all have the same MTRL
        for (ibact = 0; ibact < _cbactPart; ibact++)
        {
            if (ibset == _Ibset(ibact))
            {
                pbact = _PbactPart(ibact);
                Assert(pvNil != pbact->material, "Why does this body part "
                                                 "set have neither MTRL nor CMTL attached?");
                *ppmtrl = MTRL::PmtrlFromBmtl(pbact->material);
                AssertPo(*ppmtrl, 0);
                return;
            }
        }
        Assert(0, "why are we here?");
    }
}

/** 3DMMv1.0: *************************************************************************
    Filter callback proc for FPtInActor(). Stops when the BODY is hit.
***************************************************************************/
int BODY::_FFilterSearch(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *ray_pos, BVEC3 *ray_dir, BRS dzpNear, BRS dzpFar,
                         void *pvArg)
{
    AssertVarMem(pbact);
    AssertVarMem(ray_pos);
    AssertVarMem(ray_dir);

    PBODY pbody = (PBODY)pvArg;
    PBODY pbodyFound;
    int32_t ibset;

    AssertPo(pbody, 0);

    pbodyFound = BODY::PbodyFromBact(pbact, &ibset);
    if (pbodyFound == pvNil)
    {
        Bug("What actor is this?  It has no BODY");
        return fFalse;
    }

    AssertPo(pbodyFound, 0);

    if (pbody != pbodyFound)
        return fFalse; // 3DMMv1.0: keep searching

    pbody->_fFound = fTrue;
    if (pbact == pbody->_PbactHilite())
        return fFalse; // 3DMMv1.0: keep searching
    pbody->_ibset = ibset;
    return fTrue; // 3DMMv1.0: stop searching
}

/** 3DMMv1.0: *************************************************************************
   Returns fTrue if this body is under (xp, yp)
***************************************************************************/
bool BODY::FPtInBody(int32_t xp, int32_t yp, int32_t *pibset)
{
    AssertThis(0);

    _fFound = fFalse;
    _ibset = ivNil;
    _pbwld->IterateActorsInPt(&BODY::_FFilterSearch, (void *)this, xp, yp);
    *pibset = _ibset;
    return _fFound;
}

/** 3DMMv1.0: *************************************************************************
    BWLD is about to render the world, so clear out this BODY's _rcBounds
    (but save the last good bounds in _rcBoundsLastVis).  Also size the
    bounding box correctly.
***************************************************************************/
void BODY::_PrepareToRender(PBACT pbact)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::_PrepareToRender BEGIN actor=%p type=%u model=%p material=%p identifier=%p",
                pbact, pbact != pvNil ? (unsigned)pbact->type : 0u,
                pbact != pvNil ? pbact->model : pvNil,
                pbact != pvNil ? pbact->material : pvNil,
                pbact != pvNil ? pbact->identifier : pvNil);
#endif
    AssertVarMem(pbact);
    PBODY pbody;
    RC rc;
    BCB bcb;

    pbody = PbodyFromBact(pbact);
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::_PrepareToRender body=%p root=%p hidden=%ld", pbody,
                pbody != pvNil ? pbody->_PbactRoot() : pvNil,
                pbody != pvNil ? (long)pbody->_cactHidden : -1L);
#endif
    AssertPo(pbody, 0);

    if (pbody->FIsInView())
    {
        pbody->_rcBoundsLastVis = pbody->_rcBounds;
        pbody->_rcBounds.Zero();
    }

    // Prepare bounding box, if BODY is highlighted. Actor Studio can ask the
    // same BACT to surround one articulated part instead of the entire BODY.
    if (pbody->_PbactHilite()->type == BR_ACTOR_MODEL)
    {
        const bool fPartHilite = pbody == vpbody4DMMPartHilite &&
            FIn(vipart4DMMPartHilite, 0, pbody->_cbactPart) &&
            vcpart4DMMPartHilite > 0 &&
            vipart4DMMPartHilite + vcpart4DMMPartHilite <= pbody->_cbactPart;
        if (fPartHilite && vf4DMMPartHiliteTree)
        {
            // A Pt Group row is one hierarchy anchor, but its visible
            // selection is the complete descendant geometry. Build one box
            // from exactly those BODY nodes that descend from the anchor.
            // Model-less hierarchy nodes and temporally Cut parts contribute
            // no phantom BRender default-model bounds.
            PBACT pbactAnchor = pbody->_PbactPart(vipart4DMMPartHilite);
            bool fHaveBounds = fFalse;
            BRB brbTree;
            for (int32_t ipart = 0; ipart < pbody->_cbactPart; ++ipart)
            {
                PBACT pbactPart = pbody->_PbactPart(ipart);
                bool fMember = fFalse;
                for (PBACT pbactWalk = pbactPart; pbactWalk != pvNil &&
                     pbactWalk != pbody->_PbactRoot(); pbactWalk = pbactWalk->parent)
                {
                    if (pbactWalk == pbactAnchor)
                    {
                        fMember = fTrue;
                        break;
                    }
                }
                if (!fMember || pbactPart->model == pvNil)
                    continue;

                const BRB &brbPart = pbactPart->model->bounds;
                BMAT34 bmat34PartToRoot;
                BrActorToActorMatrix34(&bmat34PartToRoot, pbactPart, pbody->_PbactRoot());
                for (int32_t ix = 0; ix < 2; ++ix)
                    for (int32_t iy = 0; iy < 2; ++iy)
                        for (int32_t iz = 0; iz < 2; ++iz)
                        {
                            BVEC3 bvec3Local;
                            BVEC3 bvec3Root;
                            bvec3Local.v[0] = ix ? brbPart.max.v[0] : brbPart.min.v[0];
                            bvec3Local.v[1] = iy ? brbPart.max.v[1] : brbPart.min.v[1];
                            bvec3Local.v[2] = iz ? brbPart.max.v[2] : brbPart.min.v[2];
                            BrMatrix34ApplyP(&bvec3Root, &bvec3Local, &bmat34PartToRoot);
                            if (!fHaveBounds)
                            {
                                brbTree.min = bvec3Root;
                                brbTree.max = bvec3Root;
                                fHaveBounds = fTrue;
                            }
                            else
                            {
                                for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
                                {
                                    if (bvec3Root.v[iaxis] < brbTree.min.v[iaxis])
                                        brbTree.min.v[iaxis] = bvec3Root.v[iaxis];
                                    if (bvec3Root.v[iaxis] > brbTree.max.v[iaxis])
                                        brbTree.max.v[iaxis] = bvec3Root.v[iaxis];
                                }
                            }
                        }
            }
            if (fHaveBounds)
                BrBoundsToMatrix34(&pbody->_PbactHilite()->t.t.mat, &brbTree);
        }
        else if (fPartHilite && vcpart4DMMPartHilite == 1)
        {
            PBACT pbactPart = pbody->_PbactPart(vipart4DMMPartHilite);
            BRB brbPart;
            if (pbactPart->model != pvNil)
                brbPart = pbactPart->model->bounds;
            else
                BrActorToBounds(&brbPart, pbactPart);

            BMAT34 bmat34Bounds;
            BMAT34 bmat34PartToRoot;
            BrBoundsToMatrix34(&bmat34Bounds, &brbPart);
            BrActorToActorMatrix34(&bmat34PartToRoot, pbactPart, pbody->_PbactRoot());
            BrMatrix34Mul(&pbody->_PbactHilite()->t.t.mat,
                          &bmat34Bounds, &bmat34PartToRoot);
        }
        else if (fPartHilite)
        {
            // One clean box around the whole virtual Object/OG selection.
            // Imported handmade geometry is currently root-sibling topology,
            // but use actor-to-root matrices so this remains correct if BODY
            // hierarchy is introduced later.
            bool fHaveBounds = fFalse;
            BRB brbRange;
            for (int32_t ipart = vipart4DMMPartHilite;
                 ipart < vipart4DMMPartHilite + vcpart4DMMPartHilite; ++ipart)
            {
                PBACT pbactPart = pbody->_PbactPart(ipart);
                BRB brbPart;
                if (pbactPart->model != pvNil)
                    brbPart = pbactPart->model->bounds;
                else
                    BrActorToBounds(&brbPart, pbactPart);
                BMAT34 bmat34PartToRoot;
                BrActorToActorMatrix34(&bmat34PartToRoot, pbactPart, pbody->_PbactRoot());
                for (int32_t ix = 0; ix < 2; ++ix)
                    for (int32_t iy = 0; iy < 2; ++iy)
                        for (int32_t iz = 0; iz < 2; ++iz)
                        {
                            BVEC3 bvec3Local;
                            BVEC3 bvec3Root;
                            bvec3Local.v[0] = ix ? brbPart.max.v[0] : brbPart.min.v[0];
                            bvec3Local.v[1] = iy ? brbPart.max.v[1] : brbPart.min.v[1];
                            bvec3Local.v[2] = iz ? brbPart.max.v[2] : brbPart.min.v[2];
                            BrMatrix34ApplyP(&bvec3Root, &bvec3Local, &bmat34PartToRoot);
                            if (!fHaveBounds)
                            {
                                brbRange.min = bvec3Root;
                                brbRange.max = bvec3Root;
                                fHaveBounds = fTrue;
                            }
                            else
                            {
                                for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
                                {
                                    if (bvec3Root.v[iaxis] < brbRange.min.v[iaxis])
                                        brbRange.min.v[iaxis] = bvec3Root.v[iaxis];
                                    if (bvec3Root.v[iaxis] > brbRange.max.v[iaxis])
                                        brbRange.max.v[iaxis] = bvec3Root.v[iaxis];
                                }
                            }
                        }
            }
            if (fHaveBounds)
                BrBoundsToMatrix34(&pbody->_PbactHilite()->t.t.mat, &brbRange);
        }
        else
        {
            // 3DMMv1.0: Need to temporarily change type to 'none' so that the bounding
            // 3DMMv1.0: box isn't counted when calculating size of actor
            pbody->GetBcbBounds(&bcb);
            Assert(SIZEOF(BRB) == SIZEOF(BCB), "should be same structure");
            BrBoundsToMatrix34(&pbody->_PbactHilite()->t.t.mat, (BRB *)&bcb);
        }
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BODY::_PrepareToRender END actor=%p body=%p", pbact, pbody);
#endif
}

/** 3DMMv1.0: *************************************************************************
    Return the bounds of the BODY contaning PBACT
***************************************************************************/
void BODY::_GetRc(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);

    PBODY pbody;

    pbody = PbodyFromBact(pbact);
    AssertPo(pbody, 0);

    if (pvNil != pbody)
        *prc = pbody->_rcBounds;
}

/** 3DMMv1.0: *************************************************************************
    Compute the world-space bounding box of the BODY.  The code temporarily
    changes the hilite BACT's type to BR_ACTOR_NONE so that BrActorToBounds
    doesn't include the size of the bounding box when computing the size of
    the actor.
***************************************************************************/
void BODY::GetBcbBounds(BCB *pbcb, bool fWorld)
{
    AssertThis(0);
    AssertVarMem(pbcb);

    BRB brb;
    uint8_t type = _PbactHilite()->type;
    int32_t ibv3;
    br_vector3 bv3;

    _PbactHilite()->type = BR_ACTOR_NONE;
    Assert(SIZEOF(BRB) == SIZEOF(BCB), "should be same structure");
    BrActorToBounds(&brb, _PbactRoot());
    _PbactHilite()->type = type;
    *(BRB *)pbcb = brb;

    if (fWorld)
    {
        br_vector3 rgbv3[8];
        BMAT34 bmat34;

        rgbv3[0] = brb.min;
        rgbv3[1] = brb.min;
        rgbv3[1].v[0] = brb.max.v[0];
        rgbv3[2] = brb.min;
        rgbv3[2].v[1] = brb.max.v[1];
        rgbv3[3] = brb.min;
        rgbv3[3].v[2] = brb.max.v[2];
        rgbv3[4] = brb.max;
        rgbv3[5] = brb.max;
        rgbv3[5].v[0] = brb.min.v[0];
        rgbv3[6] = brb.max;
        rgbv3[6].v[1] = brb.min.v[1];
        rgbv3[7] = brb.max;
        rgbv3[7].v[2] = brb.min.v[2];

        _Get4DMMBodyWorldMatrix(this, &bmat34);

        for (ibv3 = 0; ibv3 < 8; ibv3++)
        {
            bv3.v[0] = BR_MAC4(rgbv3[ibv3].v[0], bmat34.m[0][0], rgbv3[ibv3].v[1], bmat34.m[1][0], rgbv3[ibv3].v[2],
                               bmat34.m[2][0], BR_SCALAR(1.0), bmat34.m[3][0]);
            bv3.v[1] = BR_MAC4(rgbv3[ibv3].v[0], bmat34.m[0][1], rgbv3[ibv3].v[1], bmat34.m[1][1], rgbv3[ibv3].v[2],
                               bmat34.m[2][1], BR_SCALAR(1.0), bmat34.m[3][1]);
            bv3.v[2] = BR_MAC4(rgbv3[ibv3].v[0], bmat34.m[0][2], rgbv3[ibv3].v[1], bmat34.m[1][2], rgbv3[ibv3].v[2],
                               bmat34.m[2][2], BR_SCALAR(1.0), bmat34.m[3][2]);
            rgbv3[ibv3].v[0] = bv3.v[0];
            rgbv3[ibv3].v[1] = bv3.v[1];
            rgbv3[ibv3].v[2] = bv3.v[2];
        }
        pbcb->xrMin = pbcb->yrMin = pbcb->zrMin = BR_SCALAR(10000.0);
        pbcb->xrMax = pbcb->yrMax = pbcb->zrMax = BR_SCALAR(-10000.0);
        // 3DMMv1.0: Union with body's bounds
        for (ibv3 = 0; ibv3 < 8; ibv3++)
        {
            if (rgbv3[ibv3].v[0] < pbcb->xrMin)
                pbcb->xrMin = rgbv3[ibv3].v[0];
            if (rgbv3[ibv3].v[1] < pbcb->yrMin)
                pbcb->yrMin = rgbv3[ibv3].v[1];
            if (rgbv3[ibv3].v[2] < pbcb->zrMin)
                pbcb->zrMin = rgbv3[ibv3].v[2];
            if (rgbv3[ibv3].v[0] > pbcb->xrMax)
                pbcb->xrMax = rgbv3[ibv3].v[0];
            if (rgbv3[ibv3].v[1] > pbcb->yrMax)
                pbcb->yrMax = rgbv3[ibv3].v[1];
            if (rgbv3[ibv3].v[2] > pbcb->zrMax)
                pbcb->zrMax = rgbv3[ibv3].v[2];
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    BWLD calls this function when each BACT is rendered.  It unions the
    BACT bounds with the BODY's _rcBounds
***************************************************************************/
void BODY::_BactRendered(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);
    PBODY pbody;

    pbody = PbodyFromBact(pbact);
    if (pvNil != pbody)
        pbody->_rcBounds.Union(prc);
}

/** 3DMMv1.0: *************************************************************************
    Returns whether the BODY was in view the last time it was rendered.
***************************************************************************/
bool BODY::FIsInView(void)
{
    AssertThis(0);
    return !_rcBounds.FEmpty();
}

/** 3DMMv1.0: *************************************************************************
    Fills in the 2D bounds of the BODY, the last time it was rendered.  If
    the BODY was not in view at the last render, this function fills in the
    bounds of the BODY the last time it was onstage.
***************************************************************************/
void BODY::GetRcBounds(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);

    if (FIsInView())
        *prc = _rcBounds;
    else
        *prc = _rcBoundsLastVis;
}

/** 3DMMv1.0: *************************************************************************
    Fills in the 2D coordinates of the center of the BODY, the last time
    it was rendered.  If the BODY was not in view at the last render, this
    function fills in the center of the BODY the last time it was onstage.
***************************************************************************/
void BODY::GetCenter(int32_t *pxp, int32_t *pyp)
{
    AssertThis(0);
    AssertVarMem(pxp);
    AssertVarMem(pyp);

    if (FIsInView())
    {
        *pxp = _rcBounds.XpCenter();
        *pyp = _rcBounds.YpCenter();
    }
    else
    {
        *pxp = _rcBoundsLastVis.XpCenter();
        *pyp = _rcBoundsLastVis.YpCenter();
    }
}

/** 3DMMv1.0: *************************************************************************
    Fills in the current position of the origin of this BODY.
***************************************************************************/
void BODY::GetPosition(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    BMAT34 bmat34World;
    _Get4DMMBodyWorldMatrix(this, &bmat34World);
    *pxr = bmat34World.m[3][0];
    *pyr = bmat34World.m[3][1];
    *pzr = bmat34World.m[3][2];
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the BODY.
***************************************************************************/
void BODY::AssertValid(uint32_t grf)
{
    int32_t ibact;
    int32_t ibset;
    BACT *pbact;

    BODY_PAR::AssertValid(fobjAllocated);
    AssertIn(_cactHidden, 0, 100); // 3DMMv1.0: 100 is sanity check
    AssertIn(_cbset, 0, _cbactPart + 1);
    AssertPvCb(_prgbact, LwMul(_Cbact(), SIZEOF(BACT)));
    AssertPvCb(_prgpcmtl, LwMul(_cbset, SIZEOF(PCMTL)));
    Assert(pvNil == _PbactRoot()->model, "BODY root shouldn't have a model!!");
    Assert(pvNil == _PbactRoot()->material, "BODY root shouldn't have a material!!");
    AssertPo(_pglibset, 0);
    Assert(_pglibset->CbEntry() == SIZEOF(int16_t), "bad _pglibset");

    if (grf & fobjAssertFull)
    {
        for (ibact = 0; ibact < _cbactPart; ibact++)
        {
            pbact = _PbactPart(ibact);
            if (pvNil != pbact->model)
                AssertPo(MODL::PmodlFromBmdl(pbact->model), 0);
            if (pvNil != pbact->material)
                AssertPo(MTRL::PmtrlFromBmtl(pbact->material), 0);
        }
        for (ibset = 0; ibset < _cbset; ibset++)
        {
            AssertNilOrPo(_prgpcmtl[ibset], 0);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the BODY
***************************************************************************/
void BODY::MarkMem(void)
{
    AssertThis(0);

    int32_t ibact;
    PBACT pbact;
    PMODL pmodl;
    int32_t ibset;
    bool fMtrl;
    PMTRL pmtrl;
    PCMTL pcmtl;

    BODY_PAR::MarkMem();
    MarkPv(_prgbact);
    MarkPv(_prgpcmtl);
    MarkMemObj(_pglibset);

    for (ibact = 0; ibact < _cbactPart; ibact++)
    {
        pbact = _PbactPart(ibact);
        if (pvNil != pbact->model)
        {
            pmodl = MODL::PmodlFromBmdl(pbact->model);
            AssertPo(pmodl, 0);
            MarkMemObj(pmodl);
        }
    }
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        GetPartSetMaterial(ibset, &fMtrl, &pmtrl, &pcmtl);
        if (fMtrl)
            MarkMemObj(pmtrl);
        else
            MarkMemObj(pcmtl);
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Create a blank costume -- no materials are attached yet
***************************************************************************/
COST::COST(void)
{
    TrashVar(&_cbset);
    _prgpo = pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Destroy a costume
***************************************************************************/
COST::~COST(void)
{
    AssertBaseThis(0);
    _Clear();
}

/** 3DMMv1.0: *************************************************************************
    Release all arrays and references
***************************************************************************/
void COST::_Clear(void)
{
    AssertBaseThis(0);

    int32_t ibset;

    if (pvNil != _prgpo)
    {
        for (ibset = 0; ibset < _cbset; ibset++)
            ReleasePpo(&_prgpo[ibset]); // 3DMMv1.0: Release the PCMTL or PMTRL
        FreePpv((void **)&_prgpo);
    }
    TrashVar(&_cbset);
}

/** 3DMMv1.0: *************************************************************************
    Get a costume from a BODY
***************************************************************************/
bool COST::FGet(BODY *pbody)
{
    AssertThis(0);
    AssertPo(pbody, 0);

    int32_t ibset;
    PCMTL pcmtl;
    PMTRL pmtrl;
    bool fMtrl;

    _Clear(); // 3DMMv1.0: drop previous costume, if any

    if (!FAllocPv((void **)&_prgpo, LwMul(SIZEOF(BASE *), pbody->Cbset()), fmemClear, mprNormal))
    {
        return fFalse;
    }
    _cbset = pbody->Cbset();
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        pbody->GetPartSetMaterial(ibset, &fMtrl, &pmtrl, &pcmtl);
        if (fMtrl)
            _prgpo[ibset] = pmtrl;
        else
            _prgpo[ibset] = pcmtl;
        _prgpo[ibset]->AddRef();
    }
    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Set a costume onto a BODY.  The flag fAllowDifferentShape should usually
    be fFalse; it should be fTrue in the rare cases where it is appropriate
    to apply a costume with one number of body part sets to a BODY with
    a (possibly) different number of body part sets.  In that case, the
    smaller number of materials are copied.  For example, when changing
    the number of characters in a 3-D Text object, the code grabs the
    current costume, resizes the TDT and BODY, sets the TDT's BODY to the
    default costume, then restores the old costume to the BODY as much as
    is appropriate.
***************************************************************************/
void COST::Set(PBODY pbody, bool fAllowDifferentShape)
{
    AssertThis(0);
    AssertPo(pbody, 0);

    int32_t ibset;
    BASE *po;
    int32_t cbset;

    if (fAllowDifferentShape) // 3DMMv1.0: see comment in function header
    {
        cbset = LwMin(_cbset, pbody->Cbset());
    }
    else
    {
        Assert(_cbset == pbody->Cbset(), "different BODY shapes!");
        cbset = _cbset;
    }

    for (ibset = 0; ibset < cbset; ibset++)
    {
        po = _prgpo[ibset];
        AssertPo(po, 0);
        if (po->FIs(kclsMTRL))
            pbody->SetPartSetMtrl(ibset, (PMTRL)_prgpo[ibset]);
        else
            pbody->SetPartSetCmtl((PCMTL)_prgpo[ibset]);
    }
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the COST.
***************************************************************************/
void COST::AssertValid(uint32_t grf)
{
    int32_t ibset;
    BASE *po;

    if (pvNil != _prgpo)
    {
        AssertPvCb(_prgpo, LwMul(SIZEOF(BASE *), _cbset));
        for (ibset = 0; ibset < _cbset; ibset++)
        {
            po = _prgpo[ibset];
            if (po->FIs(kclsMTRL))
                AssertPo((PMTRL)po, 0);
            else
                AssertPo((PCMTL)po, 0);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the COST
***************************************************************************/
void COST::MarkMem(void)
{
    AssertThis(0);

    int32_t ibset;

    if (pvNil != _prgpo)
    {
        MarkPv(_prgpo);
        for (ibset = 0; ibset < _cbset; ibset++)
            MarkMemObj(_prgpo[ibset]);
    }
}

#endif // 3DMMv1.0: DEBUG
