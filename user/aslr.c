/* For the ASLR bonus, tests relocations. */

#include <lib.h>
#include <stdio.h>

char *foo = "foo";

void main(int argc, char **argv) {
    printf("foo: %p\n", foo); /* invalid ptr if foo global is not relocated */
    printf("&puts: %p\n", &puts); /* also invalid */
    panic("die"); /* fails when binaryname global is not relocated properly */
}
