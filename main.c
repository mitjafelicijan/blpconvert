#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <getopt.h>
#include <libgen.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct blp2header {
	uint8_t  ident[4];             // "BLP2" magic number
	uint32_t type;                 // 0 = JPG, 1 = BLP / DXTC / Uncompressed
	uint8_t  compression;          // 1 = BLP, 2 = DXTC, 3 = Uncompressed
	uint8_t  alpha_depth;          // 0, 1, 4, or 8
	uint8_t  alpha_type;           // 0, 1, 7, or 8
	uint8_t  has_mips;             // 0 = no mips, 1 = has mips
	uint32_t width;                // Image width in pixels
	uint32_t height;               // Image height in pixels
	uint32_t mipmap_offsets[16];   // File offsets of each mipmap
	uint32_t mipmap_lengths[16];   // Length of each mipmap data block
	uint32_t palette[256];         // Color palette (256 ARGB values)
} __attribute__((packed));         // Ensure no padding issues

typedef struct {
	const char *fullname;
	char *folder;
	char *filename;                // Filename without extension
	char *extension;               // File extension (including the dot)
} path_components;

const char *type_labels[] = {
	"JPG",                         // Index 0
	"BLP/DXTC/Uncompressed"        // Index 1
};

const char *compression_labels[] = {
	"Invalid",                     // Index 0 (Unused)
	"BLP",                         // Index 1
	"DXTC",                        // Index 2
	"Uncompressed"                 // Index 3
};

typedef enum {
	PNG,
	BMP,
	TGA,
	JPG,
} image_format;

image_format get_format_type(const char *format) {
	if (strcasecmp(format, "png") == 0) return PNG;
	if (strcasecmp(format, "bmp") == 0) return BMP;
	if (strcasecmp(format, "tga") == 0) return TGA;
	if (strcasecmp(format, "jpg") == 0) return JPG;
	return PNG;
}

bool is_valid_format(const char *format) {
	const char *valid_formats[] = {"png", "bmp", "tga", "jpg", NULL};
	for (const char **f = valid_formats; *f != NULL; f++) {
		if (strcasecmp(format, *f) == 0) {
			return true;
		}
	}
	return false;
}

path_components extract_path_components(const char *filepath) {
	path_components result;
	result.fullname = filepath;
	char *filepath_copy1 = strdup(filepath);
	char *filepath_copy2 = strdup(filepath);

	result.folder = strdup(dirname(filepath_copy1));

	char *full_filename = basename(filepath_copy2);
	char *last_dot = strrchr(full_filename, '.');

	if (last_dot != NULL && last_dot != full_filename) {
		size_t name_length = last_dot - full_filename;
		result.filename = (char *)malloc(name_length + 1);
		strncpy(result.filename, full_filename, name_length);
		result.filename[name_length] = '\0';
		result.extension = strdup(last_dot);
	} else {
		result.filename = strdup(full_filename);
		result.extension = strdup("");
	}

	free(filepath_copy1);
	free(filepath_copy2);

	return result;
}

void free_path_components(path_components *path) {
	free(path->folder);
	free(path->filename);
	free(path->extension);
}

void dxt1_to_rgba(const uint8_t *dxt1_block, uint8_t *rgba_pixels) {
	uint16_t color0 = (dxt1_block[0] | (dxt1_block[1] << 8));
	uint16_t color1 = (dxt1_block[2] | (dxt1_block[3] << 8));
	uint32_t color_bits = (dxt1_block[4] | (dxt1_block[5] << 8) | (dxt1_block[6] << 16) | (dxt1_block[7] << 24));

	uint8_t r0 = ((color0 >> 11) & 0x1F) << 3;
	uint8_t g0 = ((color0 >> 5) & 0x3F) << 2;
	uint8_t b0 = (color0 & 0x1F) << 3;

	uint8_t r1 = ((color1 >> 11) & 0x1F) << 3;
	uint8_t g1 = ((color1 >> 5) & 0x3F) << 2;
	uint8_t b1 = (color1 & 0x1F) << 3;

	uint8_t colors[4][4];

	colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0; colors[0][3] = 255;
	colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1; colors[1][3] = 255;

	if (color0 > color1) {
		colors[2][0] = (2 * r0 + r1) / 3;
		colors[2][1] = (2 * g0 + g1) / 3;
		colors[2][2] = (2 * b0 + b1) / 3;
		colors[2][3] = 255;

		colors[3][0] = (r0 + 2 * r1) / 3;
		colors[3][1] = (g0 + 2 * g1) / 3;
		colors[3][2] = (b0 + 2 * b1) / 3;
		colors[3][3] = 255;
	} else {
		colors[2][0] = (r0 + r1) / 2;
		colors[2][1] = (g0 + g1) / 2;
		colors[2][2] = (b0 + b1) / 2;
		colors[2][3] = 255;

		colors[3][0] = 0;
		colors[3][1] = 0;
		colors[3][2] = 0;
		colors[3][3] = 0;
	}

	for (int i = 0; i < 16; i++) {
		uint8_t color_idx = (color_bits >> (i * 2)) & 0x3;
		uint8_t* pixel = rgba_pixels + (i * 4);
		pixel[0] = colors[color_idx][0];
		pixel[1] = colors[color_idx][1];
		pixel[2] = colors[color_idx][2];
		pixel[3] = colors[color_idx][3];
	}
}

void dxt3_to_rgba(const uint8_t *dxt3_block, uint8_t *rgba_pixels) {
	// First 8 bytes contain the alpha values (4 bits per pixel).
	uint64_t alpha_bits;
	memcpy(&alpha_bits, dxt3_block, 8);

	// Last 8 bytes contain the color data (same as DXT1 without alpha interpretation).
	dxt1_to_rgba(dxt3_block + 8, rgba_pixels);

	// Override the alpha values with the explicit values from the alpha block.
	for (int i = 0; i < 16; i++) {
		uint8_t alpha = ((alpha_bits >> (i * 4)) & 0xF) << 4 | ((alpha_bits >> (i * 4)) & 0xF);
		rgba_pixels[i * 4 + 3] = alpha;  // Set the alpha channel.
	}
}

void dxt5_to_rgba(const uint8_t *dxt5_block, uint8_t *rgba_pixels) {
	// First 8 bytes contain the interpolated alpha values.
	uint8_t alpha0 = dxt5_block[0];
	uint8_t alpha1 = dxt5_block[1];

	// Read the 6 bytes of alpha indices (48 bits total, 3 bits per pixel).
	uint64_t alpha_indices = 0;
	memcpy(&alpha_indices, dxt5_block + 2, 6);

	// Calculate alpha values table.
	uint8_t alpha_table[8];
	alpha_table[0] = alpha0;
	alpha_table[1] = alpha1;

	if (alpha0 > alpha1) {
		// 8-alpha interpolation.
		alpha_table[2] = (6 * alpha0 + 1 * alpha1) / 7;
		alpha_table[3] = (5 * alpha0 + 2 * alpha1) / 7;
		alpha_table[4] = (4 * alpha0 + 3 * alpha1) / 7;
		alpha_table[5] = (3 * alpha0 + 4 * alpha1) / 7;
		alpha_table[6] = (2 * alpha0 + 5 * alpha1) / 7;
		alpha_table[7] = (1 * alpha0 + 6 * alpha1) / 7;
	} else {
		// 6-alpha interpolation.
		alpha_table[2] = (4 * alpha0 + 1 * alpha1) / 5;
		alpha_table[3] = (3 * alpha0 + 2 * alpha1) / 5;
		alpha_table[4] = (2 * alpha0 + 3 * alpha1) / 5;
		alpha_table[5] = (1 * alpha0 + 4 * alpha1) / 5;
		alpha_table[6] = 0;
		alpha_table[7] = 255;
	}

	// Decode the color data (last 8 bytes).
	dxt1_to_rgba(dxt5_block + 8, rgba_pixels);

	// Apply alpha values.
	for (int i = 0; i < 16; i++) {
		// Extract 3-bit alpha index.
		int bit_pos = i * 3;
		int byte_pos = bit_pos / 8;
		int bit_offset = bit_pos % 8;

		uint8_t alpha_index;
		if (bit_offset > 5) {
			// Alpha index spans two bytes.
			alpha_index = (dxt5_block[2 + byte_pos] >> bit_offset) | ((dxt5_block[2 + byte_pos + 1] & ((1 << (bit_offset - 5)) - 1)) << (8 - bit_offset));
		} else {
			alpha_index = (dxt5_block[2 + byte_pos] >> bit_offset) & 0x07;
		}

		rgba_pixels[i * 4 + 3] = alpha_table[alpha_index];
	}
}

void decode_dxt_image(const uint8_t *image_data, uint32_t width, uint32_t height, int dxt_type, path_components *path, bool verbose, char *format) {
	uint32_t blocks_wide = (width + 3) / 4;
	uint32_t blocks_high = (height + 3) / 4;
	uint32_t total_pixels = width * height;
	uint32_t block_size = (dxt_type == 1) ? 8 : 16; // DXT3/5 blocks are 16 bytes, DXT1 are 8 bytes.

	uint8_t* decoded_image = (uint8_t*)malloc(total_pixels * 4);
	if (!decoded_image) {
		printf("Failed to allocate memory for decoded image\n");
		return;
	}

	// Process each block.
	for (uint32_t by = 0; by < blocks_high; by++) {
		for (uint32_t bx = 0; bx < blocks_wide; bx++) {
			uint8_t block_rgba[64];
			const uint8_t *dxt_block = image_data + (by * blocks_wide + bx) * block_size;

			switch (dxt_type) {
				case 1:
					dxt1_to_rgba(dxt_block, block_rgba);
					break;
				case 3:
					dxt3_to_rgba(dxt_block, block_rgba);
					break;
				case 5:
					dxt5_to_rgba(dxt_block, block_rgba);
					break;
			}

			for (int py = 0; py < 4; py++) {
				for (int px = 0; px < 4; px++) {
					int x = bx * 4 + px;
					int y = by * 4 + py;

					if (x >= width || y >= height) continue;

					int src_idx = (py * 4 + px) * 4;
					int dst_idx = (y * width + x) * 4;

					decoded_image[dst_idx + 0] = block_rgba[src_idx + 0];
					decoded_image[dst_idx + 1] = block_rgba[src_idx + 1];
					decoded_image[dst_idx + 2] = block_rgba[src_idx + 2];
					decoded_image[dst_idx + 3] = block_rgba[src_idx + 3];
				}
			}
		}
	}

	if (verbose) {
		printf("Saving decoded image...\n");
	}

	char output_filename[512];
	snprintf(output_filename, sizeof(output_filename), "%s/%s.%s", path->folder, path->filename, format);

	image_format fmt = get_format_type(format);
	switch (fmt) {
		case PNG:
			if (verbose) printf("Processing as PNG format\n");
			if (stbi_write_png(output_filename, width, height, 4, decoded_image, width * 4) == 0) {
				printf("Failed to write %s file\n", output_filename);
			} else {
				printf("Successfully saved %s\n", output_filename);
			}
			break;
		case BMP:
			if (verbose) printf("Processing as BMP format\n");
			if (stbi_write_bmp(output_filename, width, height, 4, decoded_image) == 0) {
				printf("Failed to write %s file\n", output_filename);
			} else {
				printf("Successfully saved %s\n", output_filename);
			}
			break;
		case TGA:
			if (verbose) printf("Processing as TGA format\n");
			if (stbi_write_tga(output_filename, width, height, 4, decoded_image) == 0) {
				printf("Failed to write %s file\n", output_filename);
			} else {
				printf("Successfully saved %s\n", output_filename);
			}
			break;
		case JPG:
			if (verbose) printf("Processing as JPG format\n");
			if (stbi_write_jpg(output_filename, width, height, 4, decoded_image, 100) == 0) {
				printf("Failed to write %s file\n", output_filename);
			} else {
				printf("Successfully saved %s\n", output_filename);
			}
			break;
	}

	// Print first few pixels for verification.
	if (verbose) {
		printf("\nFirst few pixels of decoded image (RGBA format):\n");
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				int idx = (y * width + x) * 4;
				printf("(%3d,%3d,%3d,%3d) ", 
						decoded_image[idx],
						decoded_image[idx + 1],
						decoded_image[idx + 2],
						decoded_image[idx + 3]);
			}
			printf("\n");
		}
	}

	free(decoded_image);
}

void convert_blp_file(path_components *path, bool verbose, char *format) {
	FILE *file = fopen(path->fullname, "rb");
	if (!file) {
		perror("Error opening file");
		return;
	}

	struct blp2header header;

	if (fread(&header, sizeof(struct blp2header), 1, file) != 1) {
		perror("Error reading header");
		fclose(file);
		return;
	}

	if (memcmp(header.ident, "BLP2", 4) != 0) {
		printf("Invalid BLP file!\n");
		fclose(file);
		return;
	}

	if (verbose) {
		printf("BLP File Details:\n");
		printf("  Type: %u, %s\n", header.type, type_labels[header.type]);
		printf("  Compression: %u, %s\n", header.compression, compression_labels[header.compression]);
		printf("  Alpha Depth: %u\n", header.alpha_depth);
		printf("  Alpha Type: %u\n", header.alpha_type);
		printf("  Has Mipmaps: %u\n", header.has_mips);
		printf("  Width: %u, Height: %u\n", header.width, header.height);
	}

	// Determine image data location.
	uint32_t offset = header.mipmap_offsets[0]; // First mipmap (highest resolution)
	uint32_t length = header.mipmap_lengths[0]; // Length of the mipmap

	if (offset == 0 || length == 0) {
		printf("No image data found.\n");
		fclose(file);
		return;
	}

	if (verbose) {
		printf("Reading image data at offset %u, size %u bytes\n", offset, length);
	}

	// Allocate buffer and read image data.
	uint8_t *image_data = (uint8_t *)malloc(length);
	if (!image_data) {
		perror("Memory allocation failed");
		fclose(file);
		return;
	}

	fseek(file, offset, SEEK_SET);
	if (fread(image_data, length, 1, file) != 1) {
		perror("Error reading image data");
		free(image_data);
		fclose(file);
		return;
	}

	if (header.compression == 2) {
		if (verbose) {
			printf("BLP is compressed with DXTC.\n");
			printf("Image has %d bytes.\n", length);
		}

		switch (header.alpha_type) {
			case 0: // DXT1
				decode_dxt_image(image_data, header.width, header.height, 1, path, verbose, format);
				break;
			case 1: // DXT3
				decode_dxt_image(image_data, header.width, header.height, 3, path, verbose, format);
				break;
			case 7: // DXT5
				decode_dxt_image(image_data, header.width, header.height, 5, path, verbose, format);
				break;
			default:
				printf("Unsupported alpha type: %d\n", header.alpha_type);
				break;
		}

	}

	fclose(file);
}

void print_help(const char *program_name) {
	printf("Usage: %s [OPTIONS] file1 [file2 ...]\n\n", program_name);
	printf("Options:\n");
	printf("  -h, --help           Display this help message\n");
	printf("  -v, --verbose        Enable verbose output\n");
	printf("  -f, --format=FORMAT  Set output format (default: png)\n");
	printf("                       Options: png, bmp, tga, jpg\n");
}

int main(int argc, char *argv[]) {
	bool verbose = false;
	char *format = "png";
	int c;

	static struct option long_options[] = {
		{"format",  required_argument, 0, 'f'},
		{"help",    no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	// Parse command line options
	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "f:hv", long_options, &option_index);
		if (c == -1) { break; }

		switch (c) {
			case 'h':
				print_help(argv[0]);
				return 0;
			case 'v':
				verbose = true;
				break;
			case 'f':
				if (!is_valid_format(optarg)) {
					fprintf(stderr, "Error: Invalid format '%s'. Valid formats are: png, bmp, tga, jpg\n", optarg);
					return 1;
				}
				format = optarg;
				break;
			case '?':
				return 1;
			default:
				abort();
		}
	}

	// No files specified
	if (optind >= argc) {
		fprintf(stderr, "Error: No input files specified\n");
		print_help(argv[0]);
		return 1;
	}

	// Loop though all provided files.
	while (optind < argc) {
		path_components path = extract_path_components(argv[optind++]);

		if (verbose) {
			printf("Processing File:\n");
			printf("  Fullname: %s\n", path.fullname);
			printf("  Folder: %s\n", path.folder);
			printf("  Filename: %s\n", path.filename);
			printf("  Extension: %s\n", path.extension);
			printf("  Format: %s\n", format);
		}

		convert_blp_file(&path, verbose, format);
		free_path_components(&path);
	}

	return 0;
}

