#include <stdio.h>
#include "sc15.h"
#include "version.h"

/* Split across two calls because C90 only requires a compiler to support
 * string literals up to 509 characters, and this text is longer. */
static void usage(void)
{
    fputs("  sc15 mkvce <spec.txt> <out.vce>\n"
          "        Build a voice bank. Records are copied from the MT-32 bank\n"
          "        named by the spec's source line, so your own Stunts\n"
          "        installation has to be reachable from it.\n"
          "\n",
          stderr);
    fputs("  sc15 cfg <SC15.DRV> [field=value ...]\n"
          "        Show or change the driver's configuration block.\n"
          "        Fields: port (hex), gm, gs, volume.\n"
          "\n"
          "        sc15 cfg SC15.DRV port=300\n"
          "\n"
          "  sc15 kms <song.kms> [-events]\n"
          "        Decode a song into its instruments, tracks and events.\n"
          "        Read only; used to derive the instrument mapping.\n"
          "\n"
          "  sc15 block <driver.drv>\n"
          "        Show the skidset driver block and check it against every\n"
          "        limit skidset enforces.\n",
          stderr);
}

int main(int argc, char **argv)
{
    /* On stderr, because stdout is the data channel here: kms writes a decode
     * to it and callers pipe that. */
    fputs(SC_BANNER "\n", stderr);

    if (argc < 2) {
        usage();
        return 2;
    }
    if (same_word(argv[1], "mkvce")) {
        return cmd_mkvce(argc - 2, argv + 2);
    }
    if (same_word(argv[1], "cfg")) {
        return cmd_cfg(argc - 2, argv + 2);
    }
    if (same_word(argv[1], "kms")) {
        return cmd_kms(argc - 2, argv + 2);
    }
    if (same_word(argv[1], "block")) {
        return cmd_block(argc - 2, argv + 2);
    }
    usage();
    return 2;
}
