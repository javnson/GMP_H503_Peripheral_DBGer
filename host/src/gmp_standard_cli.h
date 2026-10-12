#pragma once

/* Returns -1 if argv belongs to the legacy CLI, otherwise an exit status. */
int gmp_standard_cli(int argc, char **argv);
