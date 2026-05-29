#ifndef FILSON_GLOBBING_H
#define FILSON_GLOBBING_H

/**
 * Expand glob patterns in an array of arguments.
 * Handles: * (any chars), ? (single char), [...] (char class), ~ (home dir)
 * 
 * @param args Array of argument tokens (NULL-terminated)
 * @return New argument array with globs expanded, or original if no patterns
 *         Caller must free with filson_free_expanded_args()
 */
char **filson_expand_globs(char **args);

/**
 * Free an expanded argument array.
 * @param args Array to free
 */
void filson_free_expanded_args(char **args);

#endif
