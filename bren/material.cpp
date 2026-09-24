/* 3DMMv1.0:
 * Copyright (c) 1993 Argonaut Software Ltd. All rights reserved.
 *
 * Some test materials for the wld demo
 */
#include "argstd.h"
#include "brender.h"

br_material test_materials[] = {
    {
        "grey",
        BR_COLOUR_RGB(255, 255, 255),    /* 3DMMv1.0: colour			*/
        {255},                           /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),              /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: ks				*/
        BR_SCALAR(50),                   /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT | BR_MATF_GOURAUD, /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        0,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "grey_flat",
        BR_COLOUR_RGB(255, 255, 255), /* 3DMMv1.0: colour			*/
        {255},                        /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),           /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT,                /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        10,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "beige",
        BR_COLOUR_RGB(255, 255, 255),    /* 3DMMv1.0: colour			*/
        {255},                           /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),              /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                   /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT | BR_MATF_GOURAUD, /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        10,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "beige_flat",
        BR_COLOUR_RGB(255, 255, 255), /* 3DMMv1.0: colour			*/
        {255},                        /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),           /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT,                /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        10,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "blue",
        BR_COLOUR_RGB(255, 255, 255),    /* 3DMMv1.0: colour			*/
        {255},                           /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),              /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                   /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT | BR_MATF_GOURAUD, /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        74,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "blue_flat",
        BR_COLOUR_RGB(255, 255, 255), /* 3DMMv1.0: colour			*/
        {255},                        /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),           /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT,                /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        74,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "red",
        BR_COLOUR_RGB(255, 255, 255),    /* 3DMMv1.0: colour			*/
        {255},                           /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),              /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: ks				*/
        BR_SCALAR(70),                   /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT | BR_MATF_GOURAUD, /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        138,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "red_flat",
        BR_COLOUR_RGB(255, 255, 255), /* 3DMMv1.0: colour			*/
        {255},                        /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),           /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT,                /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        138,
        59, /* 3DMMv1.0: index base/range	*/
    },
    {
        "green",
        BR_COLOUR_RGB(255, 255, 255),    /* 3DMMv1.0: colour			*/
        {255},                           /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),              /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),              /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.40),              /* 3DMMv1.0: ks				*/
        BR_SCALAR(30),                   /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT | BR_MATF_GOURAUD, /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        202,
        44, /* 3DMMv1.0: index base/range	*/
    },
    {
        "green_flat",
        BR_COLOUR_RGB(255, 255, 255), /* 3DMMv1.0: colour			*/
        {255},                        /* 3DMMv1.0: opacity			*/
        BR_UFRACTION(0.10),           /* 3DMMv1.0: ka				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: kd				*/
        BR_UFRACTION(0.60),           /* 3DMMv1.0: ks				*/
        BR_SCALAR(20),                /* 3DMMv1.0: power			*/
        BR_MATF_LIGHT,                /* 3DMMv1.0: flags			*/
        0,
        0, /* 3DMMv1.0: shift up/down   	*/
        202,
        44, /* 3DMMv1.0: index base/range	*/
    },

};

/* 3DMMv1.0:
 * Size of the above table
 */
int test_materials_count = ASIZE(test_materials);
