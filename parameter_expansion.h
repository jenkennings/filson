#ifndef FILSON_PARAMETER_EXPANSION_H
#define FILSON_PARAMETER_EXPANSION_H

/**
 * Expand parameter references in a string.
 * Handles: ${VAR}, ${VAR:-default}, ${VAR#pattern}, ${VAR%pattern}
 * 
 * @param input String containing parameter references to expand
 * @return Newly allocated string with parameters expanded, or NULL on error
 *         Caller must free with free()
 */
char *filson_expand_parameters(const char *input);

#endif
