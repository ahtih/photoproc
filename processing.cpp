/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <string.h>
#include <ahti.hpp>
#include "processing.hpp"

/***************************************************************************/
/********************************           ********************************/
/******************************** curve_t:: ********************************/
/********************************           ********************************/
/***************************************************************************/

curve_t::curve_t(const float * const _point_values,const uint nr_of_points,
				const float _first_point_x,const float _x_step) :
							first_point_x(_first_point_x), x_step(_x_step)
{
	point_values.set_len(nr_of_points);
	for (uint i=0;i < nr_of_points;i++)
		point_values[i]=_point_values[i];
	}

float curve_t::get_value(float x) const
{
	x=(x - first_point_x) / x_step;

	if (x <= 0)
		return point_values[0];

	const uint idx=(uint)floor(x);

	if (idx >= point_values.len-1)
		return point_values[point_values.len-1];

	const float p1=point_values[idx];
	const float p2=point_values[idx+1];

	const float coeff=x - idx;

		// first choose the derivatives

	const float d1=idx ? (p2 - point_values[idx-1])/2 : 0;
	const float d2=(idx+2 < point_values.len) ?
							(point_values[idx+2] - p1)/2 : 0;

		// cubic interpolation

	return p1*(1-coeff) + p2*coeff - coeff*(1-coeff) *
					( (d2 - d1)/2 + (coeff - 0.5)*(d1 + d2 - 2*(p2 - p1)) );
	}

/***************************************************************************/
/************************                           ************************/
/************************ Lab_to_sRGB_converter_t:: ************************/
/************************                           ************************/
/***************************************************************************/

float Lab_to_sRGB_converter_t::decode_ab(const double value,
									const double func_Y,const uint negate)
{
	const double func_XY=negate ? (func_Y - value) : (func_Y + value);

	if (func_XY > 0.206893)
		return pow(func_XY,3);
	  else
		return (func_XY - 16/116.0) / 7.787;
	}

Lab_to_sRGB_converter_t::Lab_to_sRGB_converter_t(void) :
												L_table(new L_entry_t[256])
{
	for (uint scaled_L=0;scaled_L < 256;scaled_L++) {
		L_entry_t * const e=&L_table[scaled_L];

		const double L=scaled_L * (100.0 / 255);

		e->rel_Y=(L > 7.9996) ? pow((L + 16) / 116,3) : (L / 903.3);

		const double func_Y=(e->rel_Y > 0.008856) ?
						pow(e->rel_Y,1.0/3) : (7.787*e->rel_Y + 16/116.0);

		for (uint i=0;i < 256;i++) {
			e->rel_X[i]=decode_ab((i-128.0) / 500,func_Y,0);
			e->rel_Z[i]=decode_ab((i-128.0) / 200,func_Y,1);
			}
		}
	}

void Lab_to_sRGB_converter_t::convert_to_sRGB(float dest_linear_RGB[3],
					const uchar scaled_L,const schar a,const schar b) const
{
	const L_entry_t * const e=&L_table[scaled_L];

	const float rel_Y=e->rel_Y;

	const float rel_X=e->rel_X[a + 128];
	const float rel_Z=e->rel_Z[b + 128];

		/* sRGB: Rec. 709 primaries + D65 whitepoint */

	/*
	X		0.9381
	Y		0.9870
	Z		1.0746
	*/

	dest_linear_RGB[0]=( 3.241*0.9381)*rel_X - (1.537*0.9870)*rel_Y - (0.499*1.0746)*rel_Z;	// 1.205
	dest_linear_RGB[1]=(-0.969*0.9381)*rel_X + (1.876*0.9870)*rel_Y + (0.042*1.0746)*rel_Z;	// 0.949
	dest_linear_RGB[2]=( 0.056*0.9381)*rel_X - (0.204*0.9870)*rel_Y + (1.057*1.0746)*rel_Z;	// 0.909
	}

/***************************************************************************/
/*****************************                ******************************/
/***************************** crw_reader_t:: ******************************/
/*****************************                ******************************/
/***************************************************************************/

class crw_reader_t {

	void clear_vars(void) { rotate_degrees=0; shooting_info.clear(); }
	uint read_uint(file &f,const uint nr_of_bytes,const uint offset=NIL);
	uint parse_tags(file &f,const uint offset,const uint len);
		// returns zero if input file is invalid

	public:

	sint rotate_degrees;
	image_reader_t::shooting_info_t shooting_info;

	crw_reader_t(void) { clear_vars(); }
	uint open_file(const char * const fname);
		// sets shooting info variables
		// returns nonzero if file is a valid CRW file
	};

uint crw_reader_t::read_uint(file &f,const uint nr_of_bytes,const uint offset)
{
	uchar buf[sizeof(uint)];

	if (offset != NIL)
		f.Seek(offset);
	f.Read(buf,nr_of_bytes);

	uint value=0;
	for (uint i=0;i < nr_of_bytes;i++)		//!!! intel only for now
		value+=buf[i] << (i*8);

	return value;
	}

uint crw_reader_t::parse_tags(file &f,const uint offset,const uint len)
{							// returns zero if input file is invalid
	if (len < 4)
		return 0;

	const uint directory_offset=offset + read_uint(f,4,offset+len-4);
	f.Seek(directory_offset);

	const uint nr_of_tags=read_uint(f,2);

	for (uint i=0;i < nr_of_tags;i++) {

		const uint tag_type=read_uint(f,2);
		const uint taginfo_pos=f.CurPos();
		const uint tag_len=read_uint(f,4);
		const uint tag_value=read_uint(f,4);
		const uint tag_offset=offset + tag_value;

		const uint prev_pos=f.CurPos();

		if (tag_type == 0x1810 && tag_len >= 0x0c + 2) {
			const uint rotate_code=read_uint(f,2,tag_offset + 0x0c);
			switch (rotate_code) {
				case 0x10e:		rotate_degrees=-90;
								break;
				case 0x5a:		rotate_degrees=+90;
								break;
				}
			}

		if (tag_type == 0x102a && tag_len >= 4+2+2+2+2) {
			{ const uint code=read_uint(f,2,tag_offset + 4);
			shooting_info.ISO_speed=(uint)
							(100 * pow(2,(code - (float)0xa0)/0x20)); }

			{ const uint code=read_uint(f,2,tag_offset + 4+2+2);
			shooting_info.aperture=
							16 * pow(2,(code - (float)0x100)/0x40); }

			{ const uint code=read_uint(f,2,tag_offset + 4+2+2+2);
			shooting_info.exposure_time=
							1/(2000 * pow(2,(code - (float)0x160)/0x20)); }
			}

		if (tag_type == 0x5029)
			shooting_info.focal_length_mm=read_uint(f,2,taginfo_pos + 2);

		if ((tag_type >> 8) == 0x30 || (tag_type >> 8) == 0x28)
			parse_tags(f,tag_offset,tag_len);

		f.Seek(prev_pos);
		}

	return 1;
	}

uint crw_reader_t::open_file(const char * const fname)
{		// sets shooting info variables
		// returns nonzero if file is a valid CRW file

	clear_vars();

	file f(fname);

	try {
		char signature[2];

		if (f.Size() < sizeof(signature)+4+4)
			return 0;

		f.Read(signature,sizeof(signature));
		if (memcmp(signature,"II",sizeof(signature)))
			return 0;		// big-endian files are not supported for now

		const uint offset=read_uint(f,2);
		if (offset < sizeof(signature)+4 || offset > f.Size()-4)
			return 0;

		parse_tags(f,offset,f.Size()-offset);
		} catch (file_exception &e) { return 0; }

	return 1;
	}

/***************************************************************************/
/****************************                  *****************************/
/**************************** image_reader_t:: *****************************/
/****************************                  *****************************/
/***************************************************************************/

image_reader_t::image_reader_t(void) :
									Lab_converter(NULL), gamma_table(NULL) {}

image_reader_t::image_reader_t(const char * const fname) :
									Lab_converter(NULL), gamma_table(NULL)
{
	load_file(fname);
	}

void image_reader_t::load_file(const char * const fname)
{
	IMAGE::FILESOURCE filesource(fname);
	load_file(&filesource);
	}

void image_reader_t::load_file(IMAGE::SOURCE * const image_source,
									const char * const shooting_info_fname)
{
	img.Load(*image_source);

	if (img.BitDepth < 24)
		img.Convert(24);

	gamma=2.2;

	if (shooting_info_fname != NULL)
		if (*shooting_info_fname) {
			crw_reader_t crw_reader;
			if (crw_reader.open_file(shooting_info_fname)) {
				shooting_info=crw_reader.shooting_info;
				img.Rotate(crw_reader.rotate_degrees);
				if (img.BitDepth >= 48)
					gamma=1;
				}
			}

	p_step=((uint)img.BitDepth) / 8;
	reset_read_pointer();

	if (img.ColorSpace == IMAGE::CIELAB)
		Lab_converter=new Lab_to_sRGB_converter_t;
	  else {
		const uint nr_of_values=(img.BitDepth >= 48) ? 65536 : 256;
		gamma_table=new float[nr_of_values];

		const double max_value=nr_of_values-1;
		for (uint i=0;i < nr_of_values;i++)
			gamma_table[i]=pow(i / max_value,gamma);
		}

	/* measured transfer matrix:

		d30_R = 1     * screen_R + 0.264 * screen_G + 0.033 * screen_B
		d30_G = 0.152 * screen_R + 1     * screen_G + 0.292 * screen_B
		d30_B = 0.047 * screen_R + 0.411 * screen_G + 1     * screen_B
		*/

	const vec3d<double> col1={1    ,0.152,0.047};
	const vec3d<double> col2={0.264,1    ,0.1 /*!!! 0.411 */};
	const vec3d<double> col3={0.033,0.292,1    };

	const double D=determinant3(col1,col2,col3);
	const vec3d<double> vabaliige=vec3d<double>::make(1,1,1);
	const double normalize_mult[3]={
						determinant3(vabaliige,col2,col3) / D,
						determinant3(col1,vabaliige,col3) / D,
						determinant3(col1,col2,vabaliige) / D,
						};

	m.init_with_identity();
	m.x_vec=(col1 * normalize_mult[0]).tofloat();
	m.y_vec=(col2 * normalize_mult[1]).tofloat();
	m.z_vec=(col3 * normalize_mult[2]).tofloat();

	m.inverse();
	}

image_reader_t::~image_reader_t(void)
{
	if (Lab_converter != NULL) {
		delete Lab_converter;
		Lab_converter=NULL;
		}

	if (gamma_table != NULL) {
		delete [] gamma_table;
		gamma_table=NULL;
		}
	}

void image_reader_t::reset_read_pointer(void)
{
	p=(const uchar *)img.Buf;
	end_p=p + ((uint)(img.Width*img.Height)) * p_step;
	}

void image_reader_t::skip_pixels(const uint nr_of_pixels)
{
	p+=p_step * nr_of_pixels;
	}

uint image_reader_t::get_linear_RGB(float dest_rgb[3])
{			// returns 0 when image data ends

	if (p >= end_p)
		return 0;

	float v[3];
	if (img.BitDepth == 48) {
		const ushort * const pp=(const ushort *)p;
		v[0]=gamma_table[pp[2]];
		v[1]=gamma_table[pp[1]];
		v[2]=gamma_table[pp[0]];
		}
	  else {
		if (img.ColorSpace == IMAGE::CIELAB) {
			Lab_converter->convert_to_sRGB(v,p[0],
						*(const schar *)&p[1],*(const schar *)&p[2]);
			}
		  else {
			v[0]=gamma_table[p[2]];
			v[1]=gamma_table[p[1]];
			v[2]=gamma_table[p[0]];
			}
		}

	dest_rgb[0]=m.x_vec.x*v[0] + m.y_vec.x*v[1] + m.z_vec.x*v[2];
	dest_rgb[1]=m.x_vec.y*v[0] + m.y_vec.y*v[1] + m.z_vec.y*v[2];
	dest_rgb[2]=m.x_vec.z*v[0] + m.y_vec.z*v[1] + m.z_vec.z*v[2];

	p+=p_step;
	return 1;
	}

void image_reader_t::get_spot_values(
			const float x_fraction,const float y_fraction,uint dest[3]) const
{
	if (img.Buf == NULL || !img.Width || !img.Height) {
		dest[0]=dest[1]=dest[2]=0;
		return;
		}

	sint x=(sint)(x_fraction * img.Width);
	if (x < 0)
		x = 0;
	if (x > img.Width-1)
		x = img.Width-1;

	sint y=(sint)(y_fraction * img.Height);
	if (y < 0)
		y = 0;
	if (y > img.Height-1)
		y = img.Height-1;

	uint values[3];
	float best_variation=-1;
	for (uint size=4;size <= 40;size++) {
		const float variation=
					get_spot_averages((uint)x,(uint)y,values,size) / size;
		if (best_variation > variation || best_variation < 0) {
			best_variation = variation;
			dest[0]=values[0];
			dest[1]=values[1];
			dest[2]=values[2];
			}
		}

	if (img.ColorSpace == IMAGE::RGB) {		// swap BRG --> RGB
		const uint tmp=dest[0];
		dest[0]=dest[2];
		dest[2]=tmp;
		}
	}

float image_reader_t::get_spot_averages(uint x,uint y,uint dest[3],
													const uint size) const
{
	uint sum[3];
	float squares_sum[3];
	uint N=0;

	uint i;
	for (i=0;i < 3;i++) {
		sum[i]=0;
		squares_sum[i]=0;
		}

	const uint beg_x=(x < size/2) ? 0 : (x - size/2);
	const uint end_x=min(beg_x + size,(uint)img.Width);

	const uint beg_y=(y < size/2) ? 0 : (y - size/2);
	const uint end_y=min(beg_y + size,(uint)img.Height);

	for (y=beg_y;y < end_y;y++)
		for (x=beg_x;x < end_x;x++) {
			const uint idx=3 * (x + y*(uint)img.Width);

			uint value[3];
			if (img.BitDepth >= 48) {
				const ushort * const p=((const ushort *)img.Buf) + idx;
				value[0]=p[0];
				value[1]=p[1];
				value[2]=p[2];
				}
			  else {
				const uchar * const p=((const uchar *)img.Buf) + idx;
				value[0]=p[0];
				value[1]=p[1];
				value[2]=p[2];
				}

			for (uint i=0;i < 3;i++) {
				sum[i]+=value[i];
				squares_sum[i]+=value[i] * value[i];
				}

			N++;
			}

	float population_variance=0;
	for (i=0;i < 3;i++) {
		dest[i]=(sum[i] + N/2) / N;
		population_variance+=(squares_sum[i] - sum[i]*(float)sum[i]/N) / (N-1);
		}

	return population_variance;
	}

/***************************************************************************/
/**************************                       **************************/
/************************** processing_phase1_t:: **************************/
/**************************                       **************************/
/***************************************************************************/

processing_phase1_t::processing_phase1_t(image_reader_t &_image_reader,
										const uint _undo_enh_shadows) :
		image_reader(_image_reader), undo_enh_shadows(_undo_enh_shadows),
		output_line(new ushort [_image_reader.img.Width * 3 + 1]),
		output_line_end(output_line + _image_reader.img.Width * 3)
{
	image_reader.reset_read_pointer();
	}

processing_phase1_t::~processing_phase1_t(void)
{
	delete [] output_line;
	}

ushort processing_phase1_t::process_value(float value)
{
	if (value < 0)
		value = 0;

	if (undo_enh_shadows) {

			/* optimeerida mingi l„hendusvalemiga
				enh shadows v“tab aega 34.3 sek

				z  = z(x)
				x1 = 1 - x
				y  = x*x + z * (x - x*x)

				y  = x * (x + z*x1)

				z  = (x^(2/3) - x) / (1 - x)

				z l„hendatakse kui 1/3 - x1 * c1 - x1^3 * c2
				c1 = 0.08
				c2 = 0.19
				*/

		const float x=value / 0.83;
		const float x1=1 - x;
		if (x1 > 0) {
			// value=0.83 * pow(x,1/0.6);

			const float z=1.0/3 - x1 * 0.08 - x1*x1*x1 * 0.19;
			value=0.83 * x * (x + z*x1);
			}
		}

	uint uint_val=(uint)(sqrt(value) * 0xffffU);
	if (uint_val > 0xffffU)
		uint_val=0xffffU;

	return (ushort)uint_val;
	}

void processing_phase1_t::get_line(void)
{			// outputs a line of 2.0-gamma RGB ushort's

	for (ushort *p=output_line;p < output_line_end;p+=3) {
		float rgb[3];		// red, green, blue
		image_reader.get_linear_RGB(rgb);

		p[0]=process_value(rgb[0]);
		p[1]=process_value(rgb[1]);
		p[2]=process_value(rgb[2]);
		}
	}

void processing_phase1_t::skip_lines(const uint nr_of_lines)
{
	image_reader.skip_pixels(nr_of_lines * (uint)image_reader.img.Width);
	}

/***************************************************************************/
/*********************                                 *********************/
/********************* color_and_levels_processing_t:: *********************/
/*********************                                 *********************/
/***************************************************************************/

color_and_levels_processing_t::color_and_levels_processing_t(
												const params_t &_params) :
						buf(new ushort [3 * 64 * 1024]), params(_params)
{
	const float   gain_errors[3]={0,0,0};	// {0,		+0.03,		+0.12   };
	const float static_errors[3]={0,0,0};	// {+17/255.0,+15/255.0,	-5/255.0};

	const float white_clipping_density=
								params.white_clipping_stops * log10(2.0);

	for (uint c=0;c < 3;c++) {
		uint table_nr=0;
		for (;table_nr < c;table_nr++)
			if (params.color_coeffs[c] == params.color_coeffs[table_nr])
				break;

		translation_tables[c]=buf + (table_nr * 64 * 1024);
		if (table_nr != c)
			continue;

		float multiply_coeff=pow(2,params.exposure_shift) *
													params.color_coeffs[c];

		for (uint i=0;i <= 0xffffU;i++) {
			float value=i / (float)0xffffU;
			value*=value;

			value=process_value(value,gain_errors[c],static_errors[c],
							params.contrast,multiply_coeff,
							params.black_level,white_clipping_density);
			if (value < 0)
				value = 0;
			value=pow(value,1/2.2);

			sint sint_value=(sint)(value * 0xff00U);
			if (sint_value > 0xff00)
				sint_value = 0xff00;

			translation_tables[c][i]=(ushort)sint_value;
			}
		}
	}

color_and_levels_processing_t::~color_and_levels_processing_t(void)
{
	delete [] buf;
	}

float color_and_levels_processing_t::apply_shoulders(float value,float delta)
{
	float coeff=(value - (0/255.0)) / ((60 - 0)/255.0);
	if (coeff < 1) {
		if (coeff <= 0)
			return value;
		delta*=coeff;
		}
	  else {
		coeff=(1 - (50 / 255.0) - value) / ((110-50) / 255.0);
		if (coeff < 1) {
			if (coeff <= 0)
				return value;
			delta*=coeff;
			}
		}

	value+=delta;

	if (value < 0)
		return 0;
	if (value > 1)
		return 1;

	return value;
	}

float color_and_levels_processing_t::apply_soft_limit(
										const float x,const float derivative)
{
	if (x <= 0)
		return 0;
	return pow(x,derivative);
	}

float color_and_levels_processing_t::apply_soft_limits(const float value,
									float black_level,float white_level)
{
	const float level_gap=white_level - black_level;
	const float linear_value=(value - black_level) / level_gap;

	const float black_threshold=0.1;		//!!! LS-2000 asjade jaoks 0.04
	if (linear_value < black_threshold && black_level > 0.001) {
		const float threshold_value=black_level + black_threshold*level_gap;
		const float x=value / threshold_value;
		return black_threshold * apply_soft_limit(x,
							threshold_value / (level_gap * black_threshold));
		}

	const float white_threshold=1 - white_level;
	if (linear_value > 1 - white_threshold && white_level < 0.999) {
		const float threshold_value=
								1 - white_level + white_threshold*level_gap;
		const float x=(1 - value) / threshold_value;
		return 1 - white_threshold * apply_soft_limit(x,
							threshold_value / (level_gap * white_threshold));
		}

	return linear_value;
	}

float color_and_levels_processing_t::process_value(float linear_value,
					const float /*gain_error*/,const float /*static_error*/,
					const float contrast,const float multiply_coeff,
			const float black_level,const float white_clipping_density)
{			// returns linear value
	if (linear_value < 0)
		linear_value = 0;

	/*!!!
	linear_value=apply_shoulders(linear_value,
								linear_value * gain_error + static_error);
		*/

	linear_value-=black_level;
	if (linear_value <= 0)
		return 0;

	float density_value=-log10(linear_value * multiply_coeff);

	const float contrast_invariant_density=0.6;
	density_value=(density_value-contrast_invariant_density) *
									contrast + contrast_invariant_density;

	if (density_value < white_clipping_density) {
		if (white_clipping_density <= 0)
			density_value=white_clipping_density;
		  else {
			const float x=(white_clipping_density - density_value) / contrast;
			density_value=white_clipping_density *
								exp(-x * contrast/white_clipping_density);
			}
		}

	linear_value=pow(10,-density_value);
	return linear_value;
	}

void color_and_levels_processing_t::process_pixels(
				uchar *dest,const ushort *src,const uint nr_of_pixels,
				const uint output_in_BGR_format,
				const uint dest_bytes_per_pixel) const
{								//  src: 2.0-gamma 16-bit RGB
								// dest: 2.2-gamma  8-bit RGB
	uint remainder[3];
	zero(remainder);

#define DO_PROCESS_PIXELS(i,dest_c) \
		{const uint c=translation_tables[i][src[i]] + remainder[i]; \
		remainder[i]=c & 0xff; dest[dest_c]=(uchar)(c >> 8);} \

	const uchar * const dest_end=dest + dest_bytes_per_pixel*nr_of_pixels;

	if (output_in_BGR_format)
		for (;dest < dest_end;dest+=dest_bytes_per_pixel,src+=3) {
			DO_PROCESS_PIXELS(0,2);
			DO_PROCESS_PIXELS(1,1);
			DO_PROCESS_PIXELS(2,0);

			if (params.convert_to_grayscale) {
				const uchar value=(uchar)(pow((			//!!!
								pow(dest[0] / 255.0,2.2) +
								pow(dest[1] / 255.0,2.2) +
								pow(dest[2] / 255.0,2.2)) / 3,1/2.2) * 255);
				dest[0]=dest[1]=dest[2]=value;
				}
			}
	  else
		for (;dest < dest_end;dest+=dest_bytes_per_pixel,src+=3) {
			DO_PROCESS_PIXELS(0,0);
			DO_PROCESS_PIXELS(1,1);
			DO_PROCESS_PIXELS(2,2);
			}

#undef DO_PROCESS_PIXELS
	}

