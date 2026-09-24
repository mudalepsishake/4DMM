#ifndef _PMCLONE_H_
#define _PMCLONE_H_


/* BRenderModern: function typedefs */
typedef br_colour br_pixelmap_pixel_read_cbfn(const br_uint_8 *pixels, const br_pixelmap *pm, const br_pixelmap_convert_options *opts);
typedef void br_pixelmap_pixel_write_cbfn(br_uint_8 *pixels, br_colour colour, const br_pixelmap_convert_options *opts);

/* BRenderModern: pixelmap conversion structure */
typedef struct br_pixelmap_converter {

    /* BRenderModern: pixel read function */
    br_pixelmap_pixel_read_cbfn *read;

    /* BRenderModern: pixel write function */
    br_pixelmap_pixel_write_cbfn *write;

    /* BRenderModern: pixelmap type */
    br_uint_32 type;

    /* BRenderModern: pixelmap type string */
    const char *name;

} br_pixelmap_converter;

/* BRenderModern: array of pixelmap converters */
extern br_pixelmap_converter br_pixelmap_converters[];

#endif /* BRenderModern: _PMCLONE_H_ */
