/*
 * default_config.c
 * 
 * Default configuration values.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

const static struct config dfl_config = {
    .TOStitle = {
        "TOS name unknown", // F1
        "TOS name unknown", // F2
        "TOS name unknown", // F3
        "TOS name unknown", // F4
    },
    .tos = 1,
    .sound = 0,
    .boot = 1,

};


/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

