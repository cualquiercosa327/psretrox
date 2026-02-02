#ifndef MODEL_EXTRACTOR_H
#define MODEL_EXTRACTOR_H

#include <string.h>

/**
 * @brief Extracts 3D models from CRASH.BH and CRASH.BD files.
 * @param bh_path Path to the CRASH.BH file.
 * @param bd_path Path to the CRASH.BD file.
 * @param output_dir Directory to save the extracted models.
 */
void extract_models(const char* bh_path, const char* bd_path, const char* output_dir);

#endif // MODEL_EXTRACTOR_H
