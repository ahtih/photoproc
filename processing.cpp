/* Copyright (C) 2003-2005 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <string.h>
#include <stdio.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "processing.hpp"

static double determinant3(	const vec3d<double> &col1,
							const vec3d<double> &col2,
							const vec3d<double> &col3)
{
	return	  col1.x*col2.y*col3.z
			+ col1.z*col2.x*col3.y
			+ col1.y*col2.z*col3.x

			- col3.x*col2.y*col1.z
			- col3.z*col2.x*col1.y
			- col3.y*col2.z*col1.x;
	}

matrix &matrix::inverse(void) {

	matrix dest;
	vec3d<float> const x = y_vec % z_vec;
	vec3d<float> const y = z_vec % x_vec;
	vec3d<float> const z = x_vec % y_vec;
	dest.x_vec.x = x.x;
	dest.x_vec.y = y.x;
	dest.x_vec.z = z.x;
	dest.y_vec.x = x.y;
	dest.y_vec.y = y.y;
	dest.y_vec.z = z.y;
	dest.z_vec.x = x.z;
	dest.z_vec.y = y.z;
	dest.z_vec.z = z.z;

	double const determinant = x.todouble() * x_vec.todouble();
	if (fabs(determinant) >= 1.0/FLT_MAX) {
		float const coeff = (float)(1.0 / determinant);
		dest.x_vec *= coeff;
		dest.y_vec *= coeff;
		dest.z_vec *= coeff;
		}

	return (*this = dest);
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

	void clear_vars(void) { shooting_info.clear(); }
	uint read_uint(const sint fd,const uint nr_of_bytes);
	uint read_uint(const sint fd,const uint nr_of_bytes,const uint offset);
	uint read_sshort(const sint fd,const uint offset);
	uint parse_crw_tags(const sint fd,const uint offset,const uint len);
		// returns zero if input file is invalid
	uint parse_cr2_tags(const sint fd,const uint offset,const uint len);
		// returns zero if input file is invalid

	public:

	image_reader_t::shooting_info_t shooting_info;

	crw_reader_t(void) { clear_vars(); }
	uint open_file(const char * const fname);
		// sets shooting info variables
		// returns nonzero if file is a valid CRW file
	};

uint crw_reader_t::read_uint(const sint fd,const uint nr_of_bytes)
{
	uchar buf[sizeof(uint)];

	if (read(fd,buf,nr_of_bytes) != (sint)nr_of_bytes)
		return 0;

	uint value=0;
	for (uint i=0;i < nr_of_bytes;i++)		//!!! little-endian only for now
		value+=buf[i] << (i*8);

	return value;
	}

uint crw_reader_t::read_uint(const sint fd,
									const uint nr_of_bytes,const uint offset)
{
	if (lseek(fd,offset,SEEK_SET) < 0)
		return 0;

	return read_uint(fd,nr_of_bytes);
	}

uint crw_reader_t::read_sshort(const sint fd,const uint offset)
{
	const uint code=read_uint(fd,2,offset);
	return (code < 0x8000) ? code : (-(sint)(0x10000-code));
	}

uint crw_reader_t::parse_crw_tags(const sint fd,const uint offset,const uint len)
{							// returns zero if input file is invalid
	if (len < 4)
		return 0;

	const uint directory_offset=offset + read_uint(fd,4,offset+len-4);
	if (lseek(fd,directory_offset,SEEK_SET) < 0)
		return 0;

	const uint nr_of_tags=read_uint(fd,2);

	for (uint i=0;i < nr_of_tags;i++) {

		const uint tag_type=read_uint(fd,2);
		const uint taginfo_pos=(uint)lseek(fd,0,SEEK_CUR);
		const uint tag_len=read_uint(fd,4);
		const uint tag_value=read_uint(fd,4);
		const uint tag_offset=offset + tag_value;

		const uint prev_pos=(uint)lseek(fd,0,SEEK_CUR);

#if 0
		{ printf("tag 0x%x len %u  tag_value 0x%x",tag_type,tag_len,tag_value);
		if (tag_len <= 128)
			for (uint j=0;j < tag_len;j++)
				printf(" %02x",read_uint(fd,1,tag_offset + j));
		printf("\n"); }
#endif

		if (tag_type == 0x102a && tag_len >= 4+2+2+2+2) {
			{ const sint code=read_sshort(fd,tag_offset + 4);
			shooting_info.ISO_speed=(uint)
							(100 * pow(2,(code - (float)0xa0)/0x20)); }

			{ const sint code=read_sshort(fd,tag_offset + 4+2+2);
			shooting_info.aperture=
							16 * pow(2,(code - (float)0x100)/0x40); }

			{ const sint code=read_sshort(fd,tag_offset + 4+2+2+2);
			shooting_info.exposure_time=
							1/(2000 * pow(2,(code - (float)0x160)/0x20)); }

			if (tag_len >= 38+2+2) {
				{ const uint code=read_uint(fd,2,tag_offset + 38);
				if (code < 0xffffU)
					shooting_info.focused_distance_m_max=code / 100.0; }
				{ const uint code=read_uint(fd,2,tag_offset + 38+2);
				if (code < 0xffffU)
					shooting_info.focused_distance_m_min=code / 100.0; }
				}
			}

		if (tag_type == 0x5029)
			shooting_info.focal_length_mm=read_uint(fd,2,taginfo_pos + 2);

		if (tag_type == 0x80a && tag_len) {
			char camera_name_buf[32];
			const uint read_len=min(sizeof(camera_name_buf),tag_len);

			lseek(fd,tag_offset,SEEK_SET);
			if (read(fd,camera_name_buf,read_len) == (sint)read_len) {
				const void * const p=memchr(camera_name_buf,'\0',read_len);
				if (p != NULL) {
					const char * const camera_name=((const char *)p) + 1;
					if (memchr(camera_name,'\0',
							read_len - (camera_name-camera_name_buf)) != NULL)
						strcpy(shooting_info.camera_type,camera_name);
					}
				}

			if (!strcmp(shooting_info.camera_type,"Canon EOS D30") ||
				!strcmp(shooting_info.camera_type,"Canon EOS D60") ||
				!strcmp(shooting_info.camera_type,"Canon EOS 10D")) {
				shooting_info.frame_size_mm.x=22.7;
				shooting_info.frame_size_mm.y=15.1;
				}
			}

		if (tag_type == 0x180e && tag_len >= 4+4+4) {
			const time_t tim=read_uint(fd,4,tag_offset);
			struct tm _tm;
			strftime(shooting_info.timestamp,lenof(shooting_info.timestamp),
								"%d-%b-%Y %H:%M:%S",gmtime_r(&tim,&_tm));
			}

		if ((tag_type >> 8) == 0x30 || (tag_type >> 8) == 0x28)
			parse_crw_tags(fd,tag_offset,tag_len);

		if (lseek(fd,prev_pos,SEEK_SET) < 0)
			return 0;
		}

	return 1;
	}

uint crw_reader_t::parse_cr2_tags(const sint fd,const uint offset,
															const uint len)
{							// returns zero if input file is invalid
	if (len < 2+2+2+4)
		return 0;

	if (lseek(fd,offset,SEEK_SET) < 0)
		return 0;

	const uint nr_of_tags=read_uint(fd,2);

	for (uint i=0;i < nr_of_tags;i++) {

		const uint tag_type=read_uint(fd,2);
		const uint tag_value_type=read_uint(fd,2);
		const uint tag_len=read_uint(fd,4);

		uint tag_value;
		if (tag_value_type == 3 && tag_len <= 2) {
			tag_value=read_uint(fd,2);
			read_uint(fd,2);
			}
		  else
			tag_value=read_uint(fd,4);

		const uint prev_pos=(uint)lseek(fd,0,SEEK_CUR);

#if 0
		{ printf("tag 0x%x len %u  tag_value 0x%x    ",tag_type,tag_len,tag_value);
		if (tag_len <= 128)
			for (uint j=0;j < tag_len;j++)
				printf(" %02x",read_uint(fd,1,tag_value + j));
		printf("\n"); }
#endif

		if (tag_type == 0x110 && tag_len) {
			char camera_name_buf[32];
			const uint read_len=min(sizeof(camera_name_buf),tag_len);

			lseek(fd,tag_value,SEEK_SET);
			if (read(fd,camera_name_buf,read_len) == (sint)read_len) {
				if (memchr(camera_name_buf,'\0',read_len) != NULL)
					strcpy(shooting_info.camera_type,camera_name_buf);
				}

			if (!strcmp(shooting_info.camera_type,"Canon EOS 20D")) {
				shooting_info.frame_size_mm.x=22.5;
				shooting_info.frame_size_mm.y=15.0;
				}
			}

		if (lseek(fd,prev_pos,SEEK_SET) < 0)
			return 0;
		}

	return 1;
	}

uint crw_reader_t::open_file(const char * const fname)
{		// sets shooting info variables
		// returns nonzero if file is a valid CRW file

	clear_vars();

	const sint fd=open(fname,O_RDONLY);
	if (fd < 0)
		return 0;

	char signature[2];

	const uint file_size=(uint)lseek(fd,0,SEEK_END);
	if (file_size < sizeof(signature)+4+4)
		return 0;

	if (lseek(fd,0,SEEK_SET) < 0)
		return 0;

	if (read(fd,signature,sizeof(signature)) != (sint)sizeof(signature))
		return 0;

	if (memcmp(signature,"II",sizeof(signature)))
		return 0;		// big-endian files are not supported for now

	uint offset=read_uint(fd,2);
	if (offset == 42) {

			// .CR2 file

		while (1) {
			offset=read_uint(fd,4);
			if (offset > file_size) {
				printf("ZZZZ %u %u\n",offset,file_size);
				return 0;
				}
			if (!offset)
				break;
			if (!parse_cr2_tags(fd,offset,file_size-offset))
				return 0;
			}
		}
	  else {
		if (offset < sizeof(signature)+4 || offset > file_size-4)
			return 0;

		if (!parse_crw_tags(fd,offset,file_size-offset))
			return 0;
		}

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
	try {
		img.read(fname);
		} catch (Magick::Exception &e) {
			printf("Exception caught in Magick::Image::read(): %s\n",e.what());
			throw;
			}
	load_postprocess(fname);
	}

void image_reader_t::load_from_memory(const void * const buf,const uint len,
								const char * const shooting_info_fname)
{
	Magick::Blob blob;
	blob.updateNoCopy((void *)buf,len,Magick::Blob::MallocAllocator);
	try {
		img.read(blob);
		} catch (Magick::Exception &e) {
			printf("Exception caught in Magick::Image::read(): %s\n",e.what());
			throw;
			}
	load_postprocess(shooting_info_fname);
	}

void image_reader_t::load_postprocess(const char * const shooting_info_fname)
{
	gamma=2.2;

	if (shooting_info_fname != NULL)
		if (*shooting_info_fname) {
			crw_reader_t crw_reader;
			if (crw_reader.open_file(shooting_info_fname)) {
				shooting_info=crw_reader.shooting_info;
				gamma=1;
				}
			}

	img_buf=img.getConstPixels(0,0,img.columns(),img.rows());

	reset_read_pointer();

	if (0 /*!!! img.ColorSpace == IMAGE::CIELAB */)
		Lab_converter=new Lab_to_sRGB_converter_t;
	  else {
		const uint nr_of_values=1U << QuantumDepth;
		if (gamma_table == NULL)
			gamma_table=new float[nr_of_values];

		const double max_value=nr_of_values-1;
		for (uint i=0;i < nr_of_values;i++)
			gamma_table[i]=pow(i / max_value,gamma);
		}

	R_nonlinear_transfer_coeff=B_nonlinear_transfer_coeff=0;
	float R_nonlinear_mult=0,B_nonlinear_mult=0;

	vec3d<double> R_data=vec3d<double>::make(1,0,0);
	vec3d<double> G_data=vec3d<double>::make(0,1,0);
	vec3d<double> B_data=vec3d<double>::make(0,0,1);
	vec3d<double> white_balance_mult=vec3d<double>::make(1,1,1);

	if (!strcmp(shooting_info.camera_type,"Canon EOS D30")) {
		// new matrix for dcraw 7.93 -m, but without normalising
		R_data=vec3d<double>::make(1    ,0.093,0.010);
		G_data=vec3d<double>::make(0.315,1    ,0.357);
		B_data=vec3d<double>::make(0.003,0.332,1    );
		}

	if (!strcmp(shooting_info.camera_type,"Canon EOS 10D")) {
		// new matrix for dcraw 7.93 -m, with normalising
		R_data=vec3d<double>::make(1    ,0.104 ,0.047);
		G_data=vec3d<double>::make(0.383,1     ,0.386);
		B_data=vec3d<double>::make(0.039,0.301 ,  1  );
		}

	if (!strcmp(shooting_info.camera_type,"Canon EOS 20D")) {
		// new matrix for dcraw 7.93 -m, with normalising
		R_data=vec3d<double>::make(1    ,0.120 ,0.040);
		G_data=vec3d<double>::make(0.468,1     ,0.269);
		B_data=vec3d<double>::make(0.043,0.243 ,  1  );
		/*
		R_data=vec3d<double>::make(1    ,0.BBB ,0.FFF);
		G_data=vec3d<double>::make(0.AAA,1     ,0.DDD);
		B_data=vec3d<double>::make(0.EEE,0.CCC ,  1  );
		*/
		}

	if (!strcmp(shooting_info.camera_type,"Canon EOS 77D")) {
		R_data=vec3d<double>::make(1    ,0.380 ,0.157);
		G_data=vec3d<double>::make(0.336,1     ,0.338);
		B_data=vec3d<double>::make(0.085,0.482,   1  );
		white_balance_mult=vec3d<double>::make(1,7.5,1.7);
		}

	R_nonlinear_scaling=1.0 / (1-R_nonlinear_transfer_coeff*R_nonlinear_mult);
	B_nonlinear_scaling=1.0 / (1-B_nonlinear_transfer_coeff*B_nonlinear_mult);

	const double D=determinant3(R_data,G_data,B_data);
	const vec3d<double> vabaliige=vec3d<double>::make(
				(1 - R_nonlinear_transfer_coeff) * R_nonlinear_scaling,
				1,
				(1 - B_nonlinear_transfer_coeff) * B_nonlinear_scaling);
	const double normalize_mult[3]={
						determinant3(vabaliige,G_data,B_data) / D,
						determinant3(R_data,vabaliige,B_data) / D,
						determinant3(R_data,G_data,vabaliige) / D,
						};

	m.x_vec=(R_data * normalize_mult[0] * white_balance_mult.x).tofloat();
	m.y_vec=(G_data * normalize_mult[1] * white_balance_mult.y).tofloat();
	m.z_vec=(B_data * normalize_mult[2] * white_balance_mult.z).tofloat();

	m.inverse();

	/*
	printf("%.4f %.4f %.4f\n",m.x_vec.x,m.x_vec.y,m.x_vec.z);
	printf("%.4f %.4f %.4f\n",m.y_vec.x,m.y_vec.y,m.y_vec.z);
	printf("%.4f %.4f %.4f\n",m.z_vec.x,m.z_vec.y,m.z_vec.z);
	*/
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
	p=img_buf;
	end_p=p + img.columns()*img.rows();
	}

void image_reader_t::skip_pixels(const uint nr_of_pixels)
{
	p+=nr_of_pixels;
	}

uint image_reader_t::get_linear_RGB(float dest_rgb[3])
{			// returns 0 when image data ends

	if (p >= end_p)
		return 0;

	float r,g,b;
	/*!!! if (img.ColorSpace == IMAGE::CIELAB)
		Lab_converter->convert_to_sRGB(v,p[0],
						*(const schar *)&p[1],*(const schar *)&p[2]);
	  else */ {
		r=gamma_table[p->red];
		g=gamma_table[p->green];
		b=gamma_table[p->blue];
		}

		// Correct sensor nonlinear bleed

	{ const float orig_R=(r-R_nonlinear_transfer_coeff*g) * R_nonlinear_scaling;
	r=min(r,orig_R); }
	{ const float orig_B=(b-B_nonlinear_transfer_coeff*g) * B_nonlinear_scaling;
	b=min(b,orig_B); }

		// Remap sensor primaries to sRGB

	dest_rgb[0]=m.x_vec.x*r + m.y_vec.x*g + m.z_vec.x*b;
	dest_rgb[1]=m.x_vec.y*r + m.y_vec.y*g + m.z_vec.y*b;
	dest_rgb[2]=m.x_vec.z*r + m.y_vec.z*g + m.z_vec.z*b;

	p++;
	return 1;
	}

void image_reader_t::get_spot_values(
			const float x_fraction,const float y_fraction,uint dest[3]) const
{
	if (!img.columns() || !img.rows()) {
		dest[0]=dest[1]=dest[2]=0;
		return;
		}

	sint x=(sint)(x_fraction * img.columns());
	if (x < 0)
		x = 0;
	if ((uint)x > img.columns()-1)
		x = img.columns()-1;

	sint y=(sint)(y_fraction * img.rows());
	if (y < 0)
		y = 0;
	if ((uint)y > img.rows()-1)
		y = img.rows()-1;

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
	const uint end_x=min(beg_x + size,img.columns());

	const uint beg_y=(y < size/2) ? 0 : (y - size/2);
	const uint end_y=min(beg_y + size,img.rows());

	for (y=beg_y;y < end_y;y++)
		for (x=beg_x;x < end_x;x++) {
			const Magick::PixelPacket * const p=img_buf + x + y*img.columns();

			uint value[3];
			value[0]=p->red;
			value[1]=p->green;
			value[2]=p->blue;

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
		output_line(new quantum_type [_image_reader.img.columns() * 3 + 1]),
		output_line_end(output_line + _image_reader.img.columns() * 3)
{
	image_reader.reset_read_pointer();
	}

processing_phase1_t::~processing_phase1_t(void)
{
	delete [] output_line;
	}

#if PHOTOPROC_QUANTUM_BITS == 8

/* The 8-bit sqrt optimization tables are calculated by this Perl script:

#!/usr/bin/perl

use POSIX;
$FIXED_POINT_BITS=24;

sub calc_output_value($$$)
{
	my $i=shift;
	my $highest_bit_nr=shift;
	my $table_size_bits=shift;

	my $input_float_value=
			(2**($highest_bit_nr-$FIXED_POINT_BITS)) + 
			(2**($highest_bit_nr-$FIXED_POINT_BITS-$table_size_bits)) * $i;
	my $output_value=sqrt($input_float_value) * 255;
	$output_value=255 if ($output_value > 255);
	return $output_value;
	}

my $total_table_len=0;
$sqrt_data_string='';

for my $highest_bit_nr (0..31) {
	$avgerror=0;
	$table_string='';

	TABLESIZEBITS: for ($table_size_bits=0;
				$table_size_bits < $FIXED_POINT_BITS;$table_size_bits++) {

		$table_string="\n\n";
		$avgerror=0;
		my $tablesize=1 << $table_size_bits;
		my $min_output_value=calc_output_value(0,
										$highest_bit_nr,$table_size_bits);
		my $values_printed=0;
		for my $i (0..($tablesize-1)) {
			my $max_output_value=calc_output_value($i+1,
										$highest_bit_nr,$table_size_bits);
			my $maxerror=floor($max_output_value+0.5) -
											floor($min_output_value+0.5);
			next TABLESIZEBITS if ($maxerror > 1 &&
										$table_size_bits < $highest_bit_nr);
			my $output_value=floor(($min_output_value+$max_output_value)/2 + 0.5);

			if ($maxerror) {
				my $total_area=$max_output_value - $min_output_value;
				my $incorrect_area=0;
				$incorrect_area+=($output_value-0.5 - $min_output_value)
								if ($output_value-0.5 > $min_output_value);
				$incorrect_area+=($max_output_value - ($output_value+0.5))
								if ($max_output_value > ($output_value+0.5));
				$avgerror+=$incorrect_area / $total_area;
				}

			if ($values_printed >= 18) {
				$values_printed=0;
				$table_string.="\n";
				}

			$table_string.=sprintf("%3d,",$output_value);
			$values_printed++;
			$min_output_value=$max_output_value;
			}

		$table_string.="\n";
		$avgerror/=$tablesize;
		next TABLESIZEBITS if ($avgerror > 0.05 &&
										$table_size_bits < $highest_bit_nr);
		print "$table_string";
		last;
		}

	my $table_shift=$highest_bit_nr - $table_size_bits;
	$table_shift=0 if ($table_size_bits==0);
	$sqrt_data_string.="\t\t\t\t\t{$table_shift,"
					.((1 << $table_size_bits)-1).",$total_table_len},\n";
	$total_table_len+=1 << $table_size_bits;
	}

print "$sqrt_data_string";
*/

const uchar processing_phase1_t::sqrt_table[]={
  0,  0,  0,  0,  0,  0,

  1,

  1,

  1,

  1,  2,  2,  2,  2,  2,  2,  2,

  2,  2,  2,  2,  2,  3,  3,  3,

  3,  4,

  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,
  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,

  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,

  8,  8,  9,  9,  9,  9,  9, 10, 10, 10, 10, 10, 11, 11, 11, 11,

 11, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14,
 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16,

 16, 16, 17, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 19, 20, 20,
 20, 20, 20, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22,

 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25,
 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28,
 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 31,
 31, 31, 31, 31, 31, 31, 31, 32, 32, 32,

 32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34,
 34, 34, 34, 34, 35, 35, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36,
 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38,
 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 40, 40, 40,
 40, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 42,
 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 43, 43, 43, 43, 43, 43, 43, 43,
 43, 43, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45,
 45, 45,

 45, 45, 46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 48, 48, 48, 48,
 48, 48, 49, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 50, 50, 51, 51, 51,
 51, 51, 51, 52, 52, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 53, 54, 54,
 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 56,
 56, 57, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58, 58, 58, 58, 59, 59,
 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61,
 61, 61, 62, 62, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63, 63,
 64, 64,

 64, 64, 64, 65, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67, 67, 67, 68, 68,
 68, 68, 69, 69, 69, 69, 70, 70, 70, 70, 70, 71, 71, 71, 71, 72, 72, 72,
 72, 72, 73, 73, 73, 73, 74, 74, 74, 74, 74, 75, 75, 75, 75, 75, 76, 76,
 76, 76, 77, 77, 77, 77, 77, 78, 78, 78, 78, 78, 79, 79, 79, 79, 79, 80,
 80, 80, 80, 80, 81, 81, 81, 81, 81, 82, 82, 82, 82, 82, 83, 83, 83, 83,
 83, 83, 84, 84, 84, 84, 84, 85, 85, 85, 85, 85, 86, 86, 86, 86, 86, 86,
 87, 87, 87, 87, 87, 88, 88, 88, 88, 88, 88, 89, 89, 89, 89, 89, 90, 90,
 90, 90,

 90, 90, 91, 91, 91, 91, 91, 91, 92, 92, 92, 92, 92, 93, 93, 93, 93, 93,
 93, 94, 94, 94, 94, 94, 94, 95, 95, 95, 95, 95, 95, 96, 96, 96, 96, 96,
 96, 97, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 98, 98, 99, 99, 99, 99,
 99, 99,100,100,100,100,100,100,101,101,101,101,101,101,102,102,102,102,
102,102,102,103,103,103,103,103,103,104,104,104,104,104,104,104,105,105,
105,105,105,105,105,106,106,106,106,106,106,107,107,107,107,107,107,107,
108,108,108,108,108,108,108,109,109,109,109,109,109,109,110,110,110,110,
110,110,110,111,111,111,111,111,111,111,112,112,112,112,112,112,112,113,
113,113,113,113,113,113,114,114,114,114,114,114,114,115,115,115,115,115,
115,115,116,116,116,116,116,116,116,117,117,117,117,117,117,117,117,118,
118,118,118,118,118,118,119,119,119,119,119,119,119,119,120,120,120,120,
120,120,120,121,121,121,121,121,121,121,121,122,122,122,122,122,122,122,
122,123,123,123,123,123,123,123,124,124,124,124,124,124,124,124,125,125,
125,125,125,125,125,125,126,126,126,126,126,126,126,126,127,127,127,127,
127,127,127,127,

128,128,128,128,128,128,128,128,129,129,129,129,129,129,129,129,130,130,
130,130,130,130,130,130,131,131,131,131,131,131,131,131,131,132,132,132,
132,132,132,132,132,133,133,133,133,133,133,133,133,134,134,134,134,134,
134,134,134,134,135,135,135,135,135,135,135,135,136,136,136,136,136,136,
136,136,136,137,137,137,137,137,137,137,137,138,138,138,138,138,138,138,
138,138,139,139,139,139,139,139,139,139,139,140,140,140,140,140,140,140,
140,140,141,141,141,141,141,141,141,141,141,142,142,142,142,142,142,142,
142,142,143,143,143,143,143,143,143,143,143,144,144,144,144,144,144,144,
144,144,145,145,145,145,145,145,145,145,145,146,146,146,146,146,146,146,
146,146,147,147,147,147,147,147,147,147,147,148,148,148,148,148,148,148,
148,148,148,149,149,149,149,149,149,149,149,149,150,150,150,150,150,150,
150,150,150,151,151,151,151,151,151,151,151,151,151,152,152,152,152,152,
152,152,152,152,153,153,153,153,153,153,153,153,153,153,154,154,154,154,
154,154,154,154,154,154,155,155,155,155,155,155,155,155,155,155,156,156,
156,156,156,156,156,156,156,157,157,157,157,157,157,157,157,157,157,158,
158,158,158,158,158,158,158,158,158,159,159,159,159,159,159,159,159,159,
159,160,160,160,160,160,160,160,160,160,160,161,161,161,161,161,161,161,
161,161,161,162,162,162,162,162,162,162,162,162,162,162,163,163,163,163,
163,163,163,163,163,163,164,164,164,164,164,164,164,164,164,164,165,165,
165,165,165,165,165,165,165,165,165,166,166,166,166,166,166,166,166,166,
166,167,167,167,167,167,167,167,167,167,167,167,168,168,168,168,168,168,
168,168,168,168,169,169,169,169,169,169,169,169,169,169,169,170,170,170,
170,170,170,170,170,170,170,170,171,171,171,171,171,171,171,171,171,171,
172,172,172,172,172,172,172,172,172,172,172,173,173,173,173,173,173,173,
173,173,173,173,174,174,174,174,174,174,174,174,174,174,174,175,175,175,
175,175,175,175,175,175,175,175,176,176,176,176,176,176,176,176,176,176,
176,177,177,177,177,177,177,177,177,177,177,177,178,178,178,178,178,178,
178,178,178,178,178,178,179,179,179,179,179,179,179,179,179,179,179,180,
180,180,180,180,180,180,180,180,

180,181,181,181,181,181,181,182,182,182,182,182,183,183,183,183,183,183,
184,184,184,184,184,184,185,185,185,185,185,185,186,186,186,186,186,186,
187,187,187,187,187,187,188,188,188,188,188,188,189,189,189,189,189,189,
190,190,190,190,190,191,191,191,191,191,191,191,192,192,192,192,192,192,
193,193,193,193,193,193,194,194,194,194,194,194,195,195,195,195,195,195,
196,196,196,196,196,196,197,197,197,197,197,197,198,198,198,198,198,198,
199,199,199,199,199,199,199,200,200,200,200,200,200,201,201,201,201,201,
201,202,202,202,202,202,202,202,203,203,203,203,203,203,204,204,204,204,
204,204,204,205,205,205,205,205,205,206,206,206,206,206,206,206,207,207,
207,207,207,207,208,208,208,208,208,208,208,209,209,209,209,209,209,210,
210,210,210,210,210,210,211,211,211,211,211,211,212,212,212,212,212,212,
212,213,213,213,213,213,213,213,214,214,214,214,214,214,214,215,215,215,
215,215,215,216,216,216,216,216,216,216,217,217,217,217,217,217,217,218,
218,218,218,218,218,218,219,219,219,219,219,219,219,220,220,220,220,220,
220,220,221,221,221,221,221,221,221,222,222,222,222,222,222,222,223,223,
223,223,223,223,223,224,224,224,224,224,224,224,225,225,225,225,225,225,
225,226,226,226,226,226,226,226,227,227,227,227,227,227,227,228,228,228,
228,228,228,228,229,229,229,229,229,229,229,230,230,230,230,230,230,230,
230,231,231,231,231,231,231,231,232,232,232,232,232,232,232,233,233,233,
233,233,233,233,233,234,234,234,234,234,234,234,235,235,235,235,235,235,
235,236,236,236,236,236,236,236,236,237,237,237,237,237,237,237,238,238,
238,238,238,238,238,238,239,239,239,239,239,239,239,240,240,240,240,240,
240,240,240,241,241,241,241,241,241,241,242,242,242,242,242,242,242,242,
243,243,243,243,243,243,243,243,244,244,244,244,244,244,244,245,245,245,
245,245,245,245,245,246,246,246,246,246,246,246,246,247,247,247,247,247,
247,247,247,248,248,248,248,248,248,248,249,249,249,249,249,249,249,249,
250,250,250,250,250,250,250,250,251,251,251,251,251,251,251,251,252,252,
252,252,252,252,252,252,253,253,253,253,253,253,253,253,254,254,254,254,
254,254,254,254,255,255,255,255,

255,255,255,255,255,255,255,255,
};

const processing_phase1_t::sqrt_data_t processing_phase1_t::sqrt_data[32]={
					{0,0,0},
					{0,0,1},
					{0,0,2},
					{0,0,3},
					{0,0,4},
					{0,0,5},
					{0,0,6},
					{0,0,7},
					{0,0,8},
					{6,7,9},
					{7,7,17},
					{10,1,25},
					{7,31,27},
					{9,15,59},
					{10,15,75},
					{10,31,91},
					{11,31,123},
					{11,63,155},
					{11,127,219},
					{12,127,347},
					{13,127,475},
					{13,255,603},
					{13,511,859},
					{14,511,1371},
					{0,0,1883},
					{0,0,1884},
					{0,0,1885},
					{0,0,1886},
					{0,0,1887},
					{0,0,1888},
					{0,0,1889},
					{0,0,1890}
					};
#endif

quantum_type processing_phase1_t::process_value(float value)
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

	return float_sqrt_to_quantum(value);
	}

void processing_phase1_t::get_line(void)
{			// outputs a line of 2.0-gamma RGB quantums

	for (quantum_type *p=output_line;p < output_line_end;p+=3) {
		float rgb[3];		// red, green, blue
		image_reader.get_linear_RGB(rgb);

		p[0]=process_value(rgb[0]);
		p[1]=process_value(rgb[1]);
		p[2]=process_value(rgb[2]);
		}
	}

void processing_phase1_t::skip_lines(const uint nr_of_lines)
{
	image_reader.skip_pixels(nr_of_lines * image_reader.img.columns());
	}

/***************************************************************************/
/*********************                                 *********************/
/********************* color_and_levels_processing_t:: *********************/
/*********************                                 *********************/
/***************************************************************************/

color_and_levels_processing_t::color_and_levels_processing_t(
												const params_t &_params) :
					buf(new ushort [4 * (QUANTUM_MAXVAL+1)]), params(_params)
{
	const float   gain_errors[3]={0,0,0};	// {0,		+0.03,		+0.12   };
	const float static_errors[3]={0,0,0};	// {+17/255.0,+15/255.0,	-5/255.0};

	const float white_clipping_density=
								params.white_clipping_stops * log10(2.0);
	const float after_contrast_density_shift=
								params.convert_to_grayscale ? log10(3.0) : 0;

	for (uint c=0;c < 3;c++) {
		uint table_nr=0;
		for (;table_nr < c;table_nr++)
			if (params.color_coeffs[c] == params.color_coeffs[table_nr])
				break;

		translation_tables[c]=buf + table_nr*(QUANTUM_MAXVAL+1);
		if (table_nr != c)
			continue;

		float multiply_coeff=pow(2,params.exposure_shift) *
													params.color_coeffs[c];

		for (uint i=0;i <= QUANTUM_MAXVAL;i++) {
			float value=i / (float)QUANTUM_MAXVAL;
			value*=value;

			value=process_value(value,gain_errors[c],static_errors[c],
					params.contrast,multiply_coeff,
					after_contrast_density_shift,params.black_level,
					white_clipping_density + after_contrast_density_shift);
			if (value < 0)
				value = 0;

			if (params.convert_to_grayscale)
				value=sqrt(value);
			  else
				value=pow(value,1/2.2);

			sint sint_value=(sint)(value * 0xff00U);
			if (sint_value > 0xff00)
				sint_value = 0xff00;

			translation_tables[c][i]=(ushort)sint_value;
			}
		}

	grayscale_postprocessing_table=NULL;
	if (params.convert_to_grayscale) {
		grayscale_postprocessing_table=buf + 3*(QUANTUM_MAXVAL+1);

		for (uint i=0;i <= QUANTUM_MAXVAL;i++) {
			float value=i / (float)QUANTUM_MAXVAL;
			value*=value;

			value=pow(value,1/2.2);

			sint sint_value=(sint)(value * 0xff00U);
			if (sint_value > 0xff00)
				sint_value = 0xff00;

			grayscale_postprocessing_table[i]=(ushort)sint_value;
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

float color_and_levels_processing_t::apply_white_soft_clipping(
					float density_value,const float white_clipping_density)
{		// returns linear value

	if (density_value < white_clipping_density) {
		if (white_clipping_density <= 0)
			density_value=white_clipping_density;
		  else
			density_value=white_clipping_density *
						exp((density_value - white_clipping_density) /
													white_clipping_density);
		}

	return pow(10,-density_value);
	}

float color_and_levels_processing_t::process_value(float linear_value,
				const float /*gain_error*/,const float /*static_error*/,
				const float contrast,const float multiply_coeff,
				const float after_contrast_density_shift,
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

	density_value+=after_contrast_density_shift;

	return apply_white_soft_clipping(density_value,white_clipping_density);
	}

void color_and_levels_processing_t::process_pixels(
				uchar *dest,const quantum_type *src,const uint nr_of_pixels,
				const uint output_in_BGR_format,
				const uint dest_bytes_per_pixel) const
{								//  src: 2.0-gamma quantum_type RGB
								// dest: 2.2-gamma 8-bit RGB
	const uchar * const dest_end=dest + dest_bytes_per_pixel*nr_of_pixels;

#define DO_PROCESS_PIXELS(i,dest_c) \
		{const uint c=translation_tables[i][src[i]] + remainder[i]; \
		remainder[i]=c & 0xff; dest[dest_c]=(uchar)(c >> 8);} \

	if (!params.convert_to_grayscale) {
		uint remainder[3]={0,0,0};
		if (output_in_BGR_format)
			for (;dest < dest_end;dest+=dest_bytes_per_pixel,src+=3) {
				DO_PROCESS_PIXELS(0,2);
				DO_PROCESS_PIXELS(1,1);
				DO_PROCESS_PIXELS(2,0);
				}
		  else
			for (;dest < dest_end;dest+=dest_bytes_per_pixel,src+=3) {
				DO_PROCESS_PIXELS(0,0);
				DO_PROCESS_PIXELS(1,1);
				DO_PROCESS_PIXELS(2,2);
				}
		return;
		}
#undef DO_PROCESS_PIXELS

	uint remainder=0;
	for (;dest < dest_end;dest+=dest_bytes_per_pixel,src+=3) {
		float sum;
		{ const float c=translation_tables[0][src[0]]; sum =c*c; }
		{ const float c=translation_tables[1][src[1]]; sum+=c*c; }
		{ const float c=translation_tables[2][src[2]]; sum+=c*c; }

		const uint value=grayscale_postprocessing_table[
					processing_phase1_t::float_sqrt_to_quantum(
						sum * (1 / ((float)0xff00U*0xff00U))) ] + remainder;
		remainder=value & 0xff;
		dest[0]=dest[1]=dest[2]=(uchar)(value >> 8);
		}
	}

/***************************************************************************/
/************************                           ************************/
/************************ transfer matrix optimizer ************************/
/************************                           ************************/
/***************************************************************************/

#define MAX_N	20

class table_t {
	float buf[MAX_N*MAX_N];

	public:

	uint N;

	table_t(const uint n) : N(n) {}

	      float * operator [] (const uint idx)       { return &buf[idx * N]; }
	const float * operator [] (const uint idx) const { return &buf[idx * N]; }

	void set_N(const uint n)
		{
			N=n;
			}

	void flip_major_axis(void)
		{
			for (uint i=0;i < N/2 /* round down */;i++)
				for (uint j=0;j < i;j++) {
					const float tmp=(*this)[i][j];
					(*this)[i][j]=(*this)[N-1-i][j];
					(*this)[N-1-i][j]=tmp;
					}
			}

	void flip_minor_axis(void)
		{
			for (uint i=0;i < N/2 /* round down */;i++)
				for (uint j=0;j < i;j++) {
					const float tmp=(*this)[j][i];
					(*this)[j][i]=(*this)[j][N-1-i];
					(*this)[j][N-1-i]=tmp;
					}
			}

	void transpose(void)
		{
			for (uint i=0;i < N;i++)
				for (uint j=0;j < i;j++) {
					const float tmp=(*this)[i][j];
					(*this)[i][j]=(*this)[j][i];
					(*this)[j][i]=tmp;
					}
			}
	};

struct axis_t {
	const char * const name;
	const table_t &actual_table;
	const table_t &ideal_table;
	const axis_t * const other_axis;
	const uint increases_along_coord2;

	float calculate_error(const float predicted_value,
											const uint i,const uint j) const
		{		// Returns NAN if actual_table[i][j] is NAN

			const float value=actual_table[i][j];
			if (isnan(value) || isnan(predicted_value))
				return NAN;

			float reference_value=fabsf(value);
			if (!isnan(other_axis->actual_table[i][j]))
				reference_value=0.5f * (reference_value + 
									fabsf(other_axis->actual_table[i][j]));

			return (value - predicted_value) * 100.0f /
												(reference_value + 1.0f);
			}
	};

struct nonlinear_prediction_params_t {
	float linear_coeff,sensor_level_mult,nonlinear_coeff;
	};

static float calculate_error(const table_t &predicted_table,
												const axis_t * const t)
{
	float sum_of_squares=0,sum_of_weights=0;

	for (uint i=!!t->increases_along_coord2;i < predicted_table.N;i++)
		for (uint j=!t->increases_along_coord2;j < predicted_table.N;j++) {
			const float error=t->calculate_error(predicted_table[i][j],i,j);
			if (isnan(error))
				continue;
			const float weight=max(i,j);
			sum_of_squares+=error * error * weight;
			sum_of_weights+=weight;
			}

	return sqrt(sum_of_squares / sum_of_weights);
	}

static void print_error_table(const table_t &predicted_table,
												const axis_t * const t)
{
	for (uint j=0;j < predicted_table.N;j++) {
		for (uint i=0;i < predicted_table.N;i++) {
			const char *prefix="";
			if (!i)
				switch (j) {
					case 0:	prefix="X->";
							break;
					case 1:	prefix="Y";
							break;
					case 2:	prefix="|";
							break;
					case 3:	prefix="v";
							break;
					default:break;
					}
			const float error=t->calculate_error(predicted_table[i][j],i,j);
			if (isnan(error))
				printf("%s%*s",prefix,5 - (sint)strlen(prefix),".");
			else
				printf("%s%*.0f",prefix,5 - (sint)strlen(prefix),error);
			}
		printf("\n");
		}
	}

static void predict_linear_only(const float linear_coeff,table_t &dest_table,
												const axis_t * const t)
{
	for (uint i=0;i < dest_table.N;i++)
		for (uint j=0;j < dest_table.N;j++)
			dest_table[i][j]=t->ideal_table[i][j] +
							linear_coeff * t->other_axis->ideal_table[i][j];
	}

static float optimize_linear_coeff(float &best_error,const axis_t * const t)
{
	float best_linear_coeff=-777;
	best_error=1e30;

	for (uint i=0;i < 800;i++) {
		const float linear_coeff=i / 1000.0;

		table_t predicted_table(t->actual_table.N);
		predict_linear_only(linear_coeff,predicted_table,t);
		const float error=calculate_error(predicted_table,t);

		if (best_error > error) {
			best_error = error;
			best_linear_coeff=linear_coeff;
			}
		}

	return best_linear_coeff;
	}

static float optimize_linear_and_nonlinear(
					nonlinear_prediction_params_t &best_params,
					table_t &best_predicted_table,const axis_t * const t,
					const table_t &linear_predicted_other_table)
{		// returns best error

	best_params.linear_coeff=-777;
	best_params.sensor_level_mult=-777;
	best_params.nonlinear_coeff=-777;

	float best_error=1e30;

	for (uint linear_coeff_idx=0;linear_coeff_idx < 80;linear_coeff_idx++) {
		const float linear_coeff=linear_coeff_idx / 100.0;

		table_t linear_predicted_table(t->actual_table.N);
		predict_linear_only(linear_coeff,linear_predicted_table,t);

		for (uint mult_idx=10;mult_idx < 250;mult_idx++) {
			const float sensor_level_mult=mult_idx / 100.0;

			table_t sensor_difference_table(t->actual_table.N);

			{ for (uint i=0;i < t->actual_table.N;i++)
				for (uint j=0;j < t->actual_table.N;j++)
					sensor_difference_table[i][j]=max(0,
						linear_predicted_other_table[i][j] -
						linear_predicted_table[i][j] * sensor_level_mult); }

			for (uint nonlinear_coeff_idx=0;nonlinear_coeff_idx < 80;
													nonlinear_coeff_idx++) {
				const float nonlinear_coeff=nonlinear_coeff_idx / 100.0;

				table_t predicted_table(t->actual_table.N);

				{ for (uint i=0;i < t->actual_table.N;i++)
					for (uint j=0;j < t->actual_table.N;j++)
						predicted_table[i][j]=linear_predicted_table[i][j] +
							sensor_difference_table[i][j] * nonlinear_coeff; }

				const float error=calculate_error(predicted_table,t);

				if (best_error > error) {
					best_error = error;
					best_params.linear_coeff=linear_coeff;
					best_params.sensor_level_mult=sensor_level_mult;
					best_params.nonlinear_coeff=nonlinear_coeff;
					best_predicted_table=predicted_table;
					}
				}
			}
		}

	return best_error;
	}

static void improve_ideal_table(table_t &dest_table,
			const table_t &independent_table,const table_t &dependent_table,
			const uint swap_axes)
{
	uint i,j;
	uint &coord1=swap_axes ? j : i;
	uint &coord2=swap_axes ? i : j;

	for (coord1=0;coord1 < dest_table.N;coord1++) {

			// Set avg_independent and avg_dependent

		float avg_independent=0,avg_dependent=0;

		{ uint independent_count=0,dependent_count=0;
		for (coord2=0;coord2 < dest_table.N;coord2++) {
			if (!isnan(independent_table[i][j])) {
				avg_independent+=independent_table[i][j];
				independent_count++;
				}
			if (!isnan(dependent_table[i][j])) {
				avg_dependent+=dependent_table[i][j];
				dependent_count++;
				}
			}
		if (!dependent_count || !independent_count)
			continue;
		avg_independent/=independent_count;
		  avg_dependent/=  dependent_count; }

		float sum1=0,sum2=0;
		for (coord2=0;coord2 < dest_table.N;coord2++) {
			if (isnan(independent_table[i][j]) ||
											isnan(dependent_table[i][j]))
				continue;
			sum1+=	(independent_table[i][j] - avg_independent) *
					(  dependent_table[i][j] -   avg_dependent);
			sum2+=	(independent_table[i][j] - avg_independent) *
					(independent_table[i][j] - avg_independent);
			}

		coord2=0;
		const float orig_value=dependent_table[i][j];
		float value=coord1 ? (avg_dependent - avg_independent * sum1 / sum2) :
																orig_value;
		if (value < 0.8*orig_value)
			value = 0.8*orig_value;
		if (value > 1.2*orig_value)
			value = 1.2*orig_value;

		for (coord2=0;coord2 < dest_table.N;coord2++)
			dest_table[i][j]=value;
		}
	}

static void set_ideal_tables(table_t &ideal_X,table_t &ideal_Y,
							const table_t &actual_X,const table_t &actual_Y)
{
	{ for (uint i=0;i < actual_X.N;i++)
		for (uint j=0;j < actual_X.N;j++) {
			ideal_X[i][j]=actual_X[i][0];
			ideal_Y[i][j]=actual_Y[0][j];
			}}

	for (uint i=0;i < 10;i++) {
		improve_ideal_table(ideal_X,ideal_Y,actual_X,0);
		improve_ideal_table(ideal_Y,ideal_X,actual_Y,1);
		}
	}

void optimize_transfer_matrix(FILE * const input_file)
{
		/******************************/
		/*****                    *****/
		/***** Read input records *****/
		/*****                    *****/
		/******************************/

	table_t actual_X(MAX_N),actual_Y(MAX_N);

	// printf("Reading records from stdin...\n");

	uint nr_of_input_records=0;
	for (;;) {
		char line[500];
		if (fgets(line,sizeof(line),input_file) == NULL)
			break;

		uint x_value,y_value;
		if (sscanf(line,"%u %u",&x_value,&y_value) != 2)
			continue;

		actual_X[0][nr_of_input_records]=x_value;
		actual_Y[0][nr_of_input_records]=y_value;
		nr_of_input_records++;
		}

	// printf("%u records read\n",nr_of_input_records);

		/*******************************/
		/*****                     *****/
		/***** Detect N from input *****/
		/*****                     *****/
		/*******************************/

	uint N=2;
	for (;;N++) {
		if (N > MAX_N) {
			printf("Unable to deduct square side length from %u\n",
														nr_of_input_records);
			return;
			}

		if (N*N == nr_of_input_records)
			break;
		}

	// printf("Assuming %ux%u square of measurements\n",N,N);
	actual_X.set_N(N);
	actual_Y.set_N(N);

		/**************************************/
		/*****                            *****/
		/***** Rotate and normalise input *****/
		/*****                            *****/
		/**************************************/

	if ((actual_X[N-1][0]+1)*(actual_Y[N-1][0]+1) <
									(actual_X[0][0]+1)*(actual_Y[0][0]+1)) {
		// printf("Flipped minor axis\n");
		actual_X.flip_major_axis();
		actual_Y.flip_major_axis();
		}

	if ((actual_X[0][N-1]+1)*(actual_Y[0][N-1]+1) <
									(actual_X[0][0]+1)*(actual_Y[0][0]+1)) {
		// printf("Flipped major axis\n");
		actual_X.flip_minor_axis();
		actual_Y.flip_minor_axis();
		}

	if (actual_X[N-1][0]*actual_Y[0][N-1] < actual_X[0][N-1]*actual_Y[N-1][0]) {
		// printf("Transposed input table\n");
		actual_X.transpose();
		actual_Y.transpose();
		}

		// now X increases along the major axis and Y along the minor axis

	{ const float X0=actual_X[0][0];
	const float Y0=actual_Y[0][0];

	float max_value=0.0f;

	{ for (uint i=0;i < N;i++)
		for (uint j=0;j < N;j++) {
			actual_X[i][j]-=X0;
			actual_Y[i][j]-=Y0;

			if (max_value < actual_X[i][j])
				max_value = actual_X[i][j];
			if (max_value < actual_Y[i][j])
				max_value = actual_Y[i][j];
			}}

	{ for (uint i=0;i < N;i++)
		for (uint j=0;j < N;j++) {
			if (actual_X[i][j] > max_value*0.999f)
				actual_X[i][j]=NAN;
			if (actual_Y[i][j] > max_value*0.999f)
				actual_Y[i][j]=NAN;
			}}}

		/*************************************/
		/*****                           *****/
		/***** Calculate ideal_X/ideal_Y *****/
		/*****                           *****/
		/*************************************/

	table_t ideal_X(actual_X.N),ideal_Y(actual_Y.N);
	set_ideal_tables(ideal_X,ideal_Y,actual_X,actual_Y);

		/**********************************/
		/*****                        *****/
		/***** Linear only prediction *****/
		/*****                        *****/
		/**********************************/

	struct axis_info_t {
		axis_t axis;
		const axis_info_t * const other_axis;
		table_t linear_predicted_table;
		float linear_only_coeff,linear_only_error;
		nonlinear_prediction_params_t nonlinear_prediction_params;
		float nonlinear_error;
		} axis_info[2]={
			{{"X",actual_X,ideal_X,&axis_info[1].axis,0},&axis_info[1],N},
			{{"Y",actual_Y,ideal_Y,&axis_info[0].axis,1},&axis_info[0],N}};

	{ for (axis_info_t *a=&axis_info[0];a <= &axis_info[1];a++) {

		// print_error_table(a->axis.ideal_table,&a->axis);

		a->linear_only_coeff=optimize_linear_coeff(
											a->linear_only_error,&a->axis);
		printf("Linear %s prediction (input error %.1f%%): "
					"linear coeff %.3f, error map (%.3f%% remaining error):\n",
							a->axis.name,
							calculate_error(a->axis.ideal_table,&a->axis),
								a->linear_only_coeff,a->linear_only_error);

		predict_linear_only(a->linear_only_coeff,a->linear_predicted_table,
																	&a->axis);
		print_error_table(a->linear_predicted_table,&a->axis);
		}}

		/***************************************/
		/*****                             *****/
		/***** Linear+nonlinear prediction *****/
		/*****                             *****/
		/***************************************/

	{ for (axis_info_t *a=&axis_info[0];a <= &axis_info[1];a++) {
		printf("Optimizing nonlinear %s prediction...\n",a->axis.name);

		table_t nonlinear_predicted_table(N);

		a->nonlinear_error=optimize_linear_and_nonlinear(
					a->nonlinear_prediction_params,nonlinear_predicted_table,
					&a->axis,a->other_axis->linear_predicted_table);

		printf("Nonlinear %s prediction: linear coeff %.2f  "
							"sensor level mult %.2f  nonlinear coeff %.2f\n",
						a->axis.name,
						a->nonlinear_prediction_params.linear_coeff,
						a->nonlinear_prediction_params.sensor_level_mult,
						a->nonlinear_prediction_params.nonlinear_coeff);
		printf("Nonlinear prediction error map (%.3f%% average error):\n",
												a->nonlinear_error);
		print_error_table(nonlinear_predicted_table,&a->axis);
		}}

		/****************************/
		/*****                  *****/
		/***** Print conclusion *****/
		/*****                  *****/
		/****************************/

	const uint nonlinear_axis_nr=(
			(axis_info[0].linear_only_error + axis_info[1].nonlinear_error) <
			(axis_info[1].linear_only_error + axis_info[0].nonlinear_error)) ?
																		1 : 0;
	printf("\nConclusion: predict %s nonlinearly. "
										"X error %.3f%%, Y error %.3f%%\n",
			axis_info[nonlinear_axis_nr].axis.name,
			nonlinear_axis_nr ? axis_info[0].linear_only_error :
											(axis_info[0].nonlinear_error),
			(!nonlinear_axis_nr) ? axis_info[1].linear_only_error :
											(axis_info[1].nonlinear_error));
	}
