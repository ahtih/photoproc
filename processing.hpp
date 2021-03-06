/* Copyright (C) 2003-2005 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <Magick++.h>
#include "vec.hpp"

class Lab_to_sRGB_converter_t {

	struct L_entry_t {
		float rel_Y;
		float rel_X[256];	// indexed by 128 + a*
		float rel_Z[256];	// indexed by 128 + b*
		};

	L_entry_t * const L_table;

	static float decode_ab(const double value,
								const double func_Y,const uint negate);
	public:
	Lab_to_sRGB_converter_t(void);
	~Lab_to_sRGB_converter_t(void) { delete [] L_table; }

	void convert_to_sRGB(float dest_linear_RGB[3],
					const uchar scaled_L,const schar a,const schar b) const;
	};

class image_reader_t {
	const Magick::PixelPacket *img_buf;
	const Magick::PixelPacket *p;
	const Magick::PixelPacket *end_p;
	double gamma;

	Lab_to_sRGB_converter_t *Lab_converter;
	float *gamma_table;		// NULL if no table allocated
	matrix m;
	float B_nonlinear_transfer_coeff,B_nonlinear_scaling;
	float R_nonlinear_transfer_coeff,R_nonlinear_scaling;

	float get_spot_averages(uint x,uint y,uint dest[3],const uint size) const;
	void load_postprocess(const char * const shooting_info_fname=NULL);

	public:

	Magick::Image img;

	struct shooting_info_t {
		uint ISO_speed;
		float aperture;			// 2.8, 5.6 etc
		float exposure_time;	// value in seconds
		float focal_length_mm;	// <=0 if not known
		vec<float> frame_size_mm;	// <=0 if not known
		float focused_distance_m_min,focused_distance_m_max;
		char camera_type[100];
		char timestamp[100];

		void clear(void) { camera_type[0]='\0'; timestamp[0]='\0';
							ISO_speed=0;
							aperture=exposure_time=focal_length_mm=-1;
							focused_distance_m_min=focused_distance_m_max=-1;
							frame_size_mm.x=frame_size_mm.y=-1; }
		shooting_info_t(void) { clear(); }
		};
	shooting_info_t shooting_info;

	image_reader_t(void);
	image_reader_t(const char * const fname);
	void load_file(const char * const fname);
	void load_from_memory(const void * const buf,const uint len,
								const char * const shooting_info_fname=NULL);
		// buf must be allocated using malloc(); load_from_memory() will
		//  take it under it's own management, it must NOT be freed by caller

	~image_reader_t(void);

	void reset_read_pointer(void);
	uint get_linear_RGB(float dest_rgb[3]);
			// returns 0 when image data ends
	void skip_pixels(const uint nr_of_pixels);
	void get_spot_values(const float x_fraction,const float y_fraction,
														uint dest[3]) const;
	};

#ifndef PHOTOPROC_QUANTUM_BITS
#if QuantumDepth > 8
#define PHOTOPROC_QUANTUM_BITS	16
#else
#define PHOTOPROC_QUANTUM_BITS	8
#endif
#endif

#if PHOTOPROC_QUANTUM_BITS == 8
typedef uchar quantum_type;
#else
typedef ushort quantum_type;
#endif

#define QUANTUM_MAXVAL ((quantum_type)((1U << PHOTOPROC_QUANTUM_BITS)-1))

class processing_phase1_t {
	image_reader_t &image_reader;
	const uint undo_enh_shadows;

#if PHOTOPROC_QUANTUM_BITS == 8
	static const struct sqrt_data_t {
		uint shift_val,and_val,baseidx;
		uint dummy_for_alignment;
		} sqrt_data[32];

	static const uchar sqrt_table[];
#endif

	quantum_type process_value(float value);

	public:
	quantum_type * const output_line;
	const quantum_type * const output_line_end;

	processing_phase1_t(image_reader_t &_image_reader,
											const uint _undo_enh_shadows=0);
	~processing_phase1_t(void);
	void skip_lines(const uint nr_of_lines);
	void get_line(void);
			// outputs a line of 2.0-gamma RGB quantums
	static inline quantum_type float_sqrt_to_quantum(const float value) throw();
			// value must be >=0 and < 256.0
	};

class color_and_levels_processing_t {
	ushort * const buf;

	ushort * translation_tables[3];
			//  input: 0..QUANTUM_MAXVAL, gamma 2.0
			// output: 0..0xff00, gamma 2.2 for color, 2.0 for grayscale

	ushort * grayscale_postprocessing_table;
			//  input: 0..QUANTUM_MAXVAL, gamma 2.0
			// output: 0..0xff00, gamma 2.2

	static float apply_shoulders(float value,float delta);
	static float apply_soft_limit(const float x,const float derivative);
	static float apply_soft_limits(const float value,
									float black_level,float white_level);
	static float apply_white_soft_clipping(float density_value,
									const float white_clipping_density);
		// returns linear value
	static float process_value(float linear_value,
				const float gain_error,const float static_error,
				const float contrast,const float multiply_coeff,
				const float after_contrast_density_shift,
				const float black_level,const float white_clipping_density);
		// returns linear value

	public:

	struct params_t {
		float contrast;				// 1.0 for no contrast change
		float exposure_shift;		// in stops; 0 for no change
		float black_level,white_clipping_stops;
		float color_coeffs[3];		// 1.0 for no change
		uint convert_to_grayscale;	// 0 or 1
		};

	const params_t params;

	color_and_levels_processing_t(const params_t &_params);
	~color_and_levels_processing_t(void);
	void process_pixels(uchar *dest,const quantum_type *src,
			const uint nr_of_pixels,const uint output_in_BGR_format=0,
									const uint dest_bytes_per_pixel=3) const;
		//  src: 2.0-gamma quantum_type RGB
		// dest: 2.2-gamma 8-bit RGB
	};

void optimize_transfer_matrix(FILE * const input_file);

inline quantum_type processing_phase1_t::float_sqrt_to_quantum(
												const float value) throw()
{			// value must be >=0 and < 256.0

#if PHOTOPROC_QUANTUM_BITS == 8
	const uint uint_val=(uint)(value * (1U << 24));

	uint k=0;
	{ uint word=uint_val;
	if (word > 0x0000ffff) { word>>=16; k+=16; }
	if (word > 0x000000ff) { word>>=8;  k+=8;  }
	if (word > 0x0000000f) { word>>=4;  k+=4;  }
	if (word > 0x00000003) { word>>=2;  k+=2;  }
	if (word > 0x00000001) k++; }

		// k is 0..31 here, as the number of the highest bit set in uint_val
		// if uint_val > 0x80000000U, k=31; if uint_val < 1, k=0

	const sqrt_data_t * const data=&sqrt_data[k];
	return sqrt_table[
			((uint_val >> data->shift_val) & data->and_val) + data->baseidx ];
#else
	uint uint_val=(uint)(sqrt(value) * QUANTUM_MAXVAL);
	if (uint_val > QUANTUM_MAXVAL)
		uint_val = QUANTUM_MAXVAL;

	return (quantum_type)uint_val;
#endif
	}
