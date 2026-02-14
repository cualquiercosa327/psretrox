#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Extracts 3D model blocks from paired header/data files (e.g. CRASH.BH/CRASH.BD).
 *
 * The header file contains an array of 12-byte entries:
 *   offset (u32 LE) | size (u32 LE) | type (u32 LE)
 *
 * Each entry describes a block inside the data file. Blocks are extracted
 * as individual .bin files into the output directory.
 *
 * @param bh_path   Path to the header file (index entries)
 * @param bd_path   Path to the data file (raw block data)
 * @param output_dir Directory to write extracted blocks
 */
void extract_models(const char *bh_path, const char *bd_path, const char *output_dir);

/**
 * @brief Extracts audio tracks from paired header/data files (e.g. MUSIC.MH/MUSIC.MB).
 *
 * The header file contains an array of 8-byte entries:
 *   offset (u32 LE) | size (u32 LE)
 *
 * Each entry describes an audio track inside the data file. Tracks are
 * extracted as individual .vag files (PS2 ADPCM format).
 *
 * @param mb_path    Path to the music data file (raw audio)
 * @param mh_path    Path to the music header file (index entries)
 * @param output_dir Directory to write extracted tracks
 */
void extract_audio_tracks(const char *mb_path, const char *mh_path, const char *output_dir);

#endif /* ASSETS_H */
