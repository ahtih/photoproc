/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <string.h>
#include "processing.hpp"
#include "interactive-processor.hpp"

#define MEASURE_PASS2_TIME	0

	/*	processing pipeline:

			img, image_reader_t		hoitakse alati mälus

			enh shadows processing
				[linear float]
			sharpen

				[siit saab küsida reakaupa 16-bitises 2.0 gammaga RGB's]

				[GUI teeb siin resize, ja cacheb tulemust]

			color & levels processing (with tables)

				[siit saab küsida reakaupa 8-bitises 2.2 gammaga RGB's]
		*/

/***************************************************************************/
/*******************************             *******************************/
/******************************* SyncQueue:: *******************************/
/*******************************             *******************************/
/***************************************************************************/

SyncQueue::SyncQueue(void) : Head(NULL), Tail(NULL) {}

SyncQueue::~SyncQueue(void)
{
	QMutexLocker rm(&mutex);
	for (Element *e=Head;e != NULL;) {
		Element *n=e->Next;
		delete [] (char *) e->DataPtr();
		e=n;
		}
	}

void SyncQueue::Write(const void *ptr,uint len)
{
	char * const data=new char[len + sizeof(Element)];
	memcpy(data,ptr,len);

	Element * const e=(Element *)(data+len);
	e->Len=len;
	e->Next=NULL;

	QMutexLocker rm(&mutex);
	if (Tail != NULL) {
		Tail->Next=e;
		Tail=e;
		}
	  else
		Tail=Head=e;

	cond.wakeAll();
	}

void *SyncQueue::Read(uint &len,const uint no_wait)
{
	QMutexLocker rm(&mutex);

	while (1) {

		if (Head != NULL) {
			Element * const e=Head;
			Head=Head->Next;
			if (Head == NULL)
				Tail=NULL;
			len=e->Len;
			return e->DataPtr();
			}

		if (no_wait)
			break;

		cond.wait(&mutex);
		}

	return NULL;
	}

/***************************************************************************/
/*********************                                 *********************/
/********************* interactive_image_processor_t:: *********************/
/*********************                                 *********************/
/***************************************************************************/

void interactive_image_processor_t::set_enh_shadows(
											const uint _undo_enh_shadows)
{
	params.undo_enh_shadows=_undo_enh_shadows;
	ensure_processing_level(PASS1);
	}

void interactive_image_processor_t::set_crop(
					const uint top_pixels,const uint bottom_pixels,
					const uint left_pixels,const uint right_pixels)
{
	params.top_crop=top_pixels;
	params.bottom_crop=bottom_pixels;
	params.left_crop=left_pixels;
	params.right_crop=right_pixels;
	ensure_processing_level(PASS1);
	}

void interactive_image_processor_t::set_color_and_levels_params(
					const color_and_levels_processing_t::params_t &_params)
{
	params.color_and_levels_params=_params;
	ensure_processing_level(PASS2);
	}

interactive_image_processor_t::interactive_image_processor_t(
		notification_receiver_t * const _notification_receiver) :
			notification_receiver(_notification_receiver),
			lowres_phase1_image(NULL),
			operation_pending_count(0), is_processing_necessary(0),
			is_file_loaded(0)
{
	params.required_level=NEW_LOWRES_BUF;
	params.output_buf=NULL;
	params.output_in_BGR_format=0;
	params.dest_bytes_per_pixel=0;
	params.working_x_size=0;
	params.working_y_size=0;
	params.undo_enh_shadows=0;
	params.top_crop=params.bottom_crop=params.left_crop=params.right_crop=0;

	start();
	}

interactive_image_processor_t::~interactive_image_processor_t(void)
{
	Write(NULL,0);
	wait(20*1000);

	if (lowres_phase1_image != NULL)
		delete [] lowres_phase1_image;
	}

void interactive_image_processor_t::ensure_processing_level(
											const required_level_t level)
{
	if ((sint)params.required_level < (sint)level)
		params.required_level=level;

	is_processing_necessary=1;
	}

void interactive_image_processor_t::set_working_res(
			const uint x_size,const uint y_size,uchar * const output_buf,
			const uint output_in_BGR_format,const uint dest_bytes_per_pixel)
{
	params.working_x_size=x_size;
	params.working_y_size=y_size;
	params.output_buf=output_buf;
	params.output_in_BGR_format=output_in_BGR_format;
	params.dest_bytes_per_pixel=dest_bytes_per_pixel;
	ensure_processing_level(NEW_LOWRES_BUF);
	}

void interactive_image_processor_t::start_operation(
				const operation_type_t operation_type,const char * const fname,
				void * const param_ptr,const uint param_uint)
{
	cmd_packet_t packet;
	packet.operation_type=operation_type;
	packet.params=params;
	packet.param_ptr=param_ptr;
	packet.param_uint=param_uint;
	packet.fname[0]='\0';
	if (fname != NULL) {
		memcpy(packet.fname,fname,
				(strlen(fname) < sizeof(packet.fname)) ? strlen(fname)+1 :
														sizeof(packet.fname));
		packet.fname[sizeof(packet.fname)-1]='\0';
		}

	Write(&packet,sizeof(packet));

	if (operation_type == PROCESSING) {
		params.required_level=PASS2;
		is_processing_necessary=0;
		}

	operation_pending_count++;
	}

void interactive_image_processor_t::run(void)
{
	while (1) {
		uint len;
		void *ptr=Read(len);

		const cmd_packet_t * const packet=(const cmd_packet_t *)ptr;
		if (len != sizeof(*packet)) {
			Release(ptr);
			break;
			}

		result_t result;
		result.operation_type=packet->operation_type;
		result.error_text=NULL;

		if (packet->operation_type == LOAD_FILE) {
			QMutexLocker req(&image_load_mutex);
			image_reader.load_file(packet->fname);
			}
		if (packet->operation_type == LOAD_FROM_MEMORY) {
			QMutexLocker req(&image_load_mutex);
			image_reader.load_from_memory(packet->param_ptr,
									packet->param_uint,packet->fname);
			}
		else
		if (packet->operation_type == PROCESSING)
			do_processing(packet->params);
		else
		if (packet->operation_type == FULLRES_PROCESSING)
			do_fullres_processing(packet->params,packet->fname);

		results_queue.Write(&result,sizeof(result));
		notification_receiver->operation_completed();

		Release(ptr);
		}
	}

uint interactive_image_processor_t::get_operation_results(
						operation_type_t &operation_type,char * &error_text)
{			// returns 0 if no operation results are available
			// error_text has to be delete []'d by caller

	uint len;
	void * const ptr=results_queue.Read(len,1);
	if (ptr == NULL || len != sizeof(result_t))
		return 0;

	const result_t * const result=(const result_t *)ptr;
	operation_type=result->operation_type;
	error_text=result->error_text;

	operation_pending_count--;
	if (operation_type == LOAD_FILE || operation_type == LOAD_FROM_MEMORY) {
		is_file_loaded=(error_text == NULL);
		if (is_file_loaded)
			ensure_processing_level(PASS1);
		}

	return 1;
	}

void interactive_image_processor_t::resize_line(
					ushort *dest_p,const uint dest_size,
					const uint *src_p,const uint src_size)
{
		//!!! mis siis kui src_size <= dest_size

	uint  src_mult_value=0;		// src_x * dest_size
	uint dest_mult_value=0;		// dest_x * src_size
	for (const ushort * const dest_p_end=dest_p + 3*dest_size;
											dest_p < dest_p_end;dest_p+=3) {
		dest_mult_value+=src_size;
		uint count=0;
		uint dest_c0=0,dest_c1=0,dest_c2=0;
		while (src_mult_value < dest_mult_value) {
			dest_c0+=src_p[0];
			dest_c1+=src_p[1];
			dest_c2+=src_p[2];
			src_p+=3;
			src_mult_value+=dest_size;
			count++;
			}
		if (count >= 2) {
							//!!! optimeerida shifti ja muli/tabeliga
			dest_p[0]=(ushort)(dest_c0 / count);
			dest_p[1]=(ushort)(dest_c1 / count);
			dest_p[2]=(ushort)(dest_c2 / count);
			}
		  else {
			dest_p[0]=(ushort)dest_c0;
			dest_p[1]=(ushort)dest_c1;
			dest_p[2]=(ushort)dest_c2;
			}
		}
	}

void interactive_image_processor_t::do_processing(const params_t par)
{
	if ((sint)par.required_level >= (sint)NEW_LOWRES_BUF) {
		if (lowres_phase1_image != NULL)
			delete [] lowres_phase1_image;
		lowres_phase1_image=
					new ushort [par.working_x_size * par.working_y_size * 3];
		}

	if ((sint)par.required_level >= (sint)PASS1) {
		processing_phase1_t phase1(image_reader,par.undo_enh_shadows);

		const vec<uint> src_size=get_image_size(&par);

		uint * const sum_buf=new uint [src_size.x * 3];
		const uint * const sum_buf_end=sum_buf + (src_size.x * 3);

			//!!! mis siis kui src_size.y <= par.working_y_size

		phase1.skip_lines(par.top_crop);

		uint  src_mult_value=0;		// src_y * dest_size
		uint dest_mult_value=0;		// dest_y * src_size
		for (uint dest_y=0;dest_y < par.working_y_size;dest_y++) {
			dest_mult_value+=src_size.y;

			memset(sum_buf,'\0',src_size.x * 3 * sizeof(*sum_buf));
			uint count=0;
			while (src_mult_value < dest_mult_value) {
				phase1.get_line();
				const ushort * const output_line_start=
										phase1.output_line + par.left_crop*3;
				const ushort * const output_line_end=
										output_line_start + src_size.x*3;
				uint *q=sum_buf;
				for (const ushort *p=output_line_start;p < output_line_end;
																p+=3,q+=3) {
					q[0]+=p[0];
					q[1]+=p[1];
					q[2]+=p[2];
					}

				src_mult_value+=par.working_y_size;
				count++;
				}
			if (count >= 2) {
				if (!(count & (count-1))) {
					uint shift=0;
					for (;(1U << shift) < count;shift++)
						;
					for (uint *q=sum_buf;q < sum_buf_end;q+=3) {
						q[0]>>=shift;
						q[1]>>=shift;
						q[2]>>=shift;
						}
					}
				  else
					for (uint *q=sum_buf;q < sum_buf_end;q+=3) {
						q[0]/=count;		//!!! optimeerida muli ja tabeliga
						q[1]/=count;
						q[2]/=count;
						}
				}

			resize_line(lowres_phase1_image + (dest_y*par.working_x_size*3),
									par.working_x_size,sum_buf,src_size.x);
			}

		delete [] sum_buf;
		}

	if ((sint)par.required_level >= (sint)PASS2) {
#if MEASURE_PASS2_TIME
		timer tim;
#endif
		const color_and_levels_processing_t pass2(par.color_and_levels_params);
#if MEASURE_PASS2_TIME
		const float init_time=tim.lap();
#endif
		pass2.process_pixels(par.output_buf,lowres_phase1_image,
					par.working_x_size * par.working_y_size,
					par.output_in_BGR_format,par.dest_bytes_per_pixel);
		// draw_gamma_test_image(par);
		// draw_processing_curve(par);

#if MEASURE_PASS2_TIME
		DbgPrintf("pass2: init time: %gms processing time: %gms\n",
													init_time,tim.lap());
#endif
		}
	}

void interactive_image_processor_t::draw_gamma_test_image(const params_t par) const
{
	const float gamma=par.color_and_levels_params.contrast;		//!!!

	for (uint y=0;y < par.working_y_size;y++) {
		const uchar raster_value=(uchar)(0xff - (y*0x100/par.working_y_size));
		const float raster_linear_value=
									pow(raster_value / (float)0xff,gamma);

		const uchar solid_value=(uchar)floor(
						pow(raster_linear_value/2,1 / gamma) * 0xff + 0.5);

		uchar *p=par.output_buf +
						par.dest_bytes_per_pixel * (y*par.working_x_size);

		uint color_band_width=par.working_x_size / (3+1);
		const uint strip_width=(color_band_width + 5/2) / 5;
		color_band_width=strip_width * 5;
		for (uint x=0;x < par.working_x_size;
									x++,p+=par.dest_bytes_per_pixel) {
			if ((x / strip_width) & 1)
				p[0]=p[1]=p[2]=solid_value;
			  else
				p[0]=p[1]=p[2]=((/*x*/ + y) & 1) ? raster_value : 0;

			switch (x / color_band_width) {
				case 0:		p[1]=p[2]=0;	break;
				case 1:		p[0]=p[2]=0;	break;
				case 2:		p[0]=p[1]=0;	break;
				}
			}
		}
	}

void interactive_image_processor_t::draw_processing_curve(const params_t par) const
{
	memset(par.output_buf,0xff,
		par.working_x_size * par.working_y_size * par.dest_bytes_per_pixel);

	const color_and_levels_processing_t pass2(par.color_and_levels_params);

	for (uint x=0;x < par.working_x_size;x++) {
		const float src_density=4.0 * (1 - x / (float)(par.working_x_size-1));

		const uint src_value=(uint)floor(0xffff *
										sqrt(pow(10,-src_density)) + 0.5);
		ushort src_rgb[3];
		src_rgb[0]=src_rgb[1]=src_rgb[2]=(ushort)(
								(src_value <= 0xffff) ? src_value : 0xffff);
		uchar dest_rgb[3];
		pass2.process_pixels(dest_rgb,src_rgb,1,0,sizeof(dest_rgb));

		for (uint c=0;c < lenof(dest_rgb);c++) {
			uint y=par.working_y_size-1;
			if (dest_rgb[c]) {
				const float dest_density=-log10(pow(dest_rgb[c] / 255.0,2.2));
				y=(uint)(dest_density / 4.0 * par.working_y_size);
				if (y > par.working_y_size-1)
					y = par.working_y_size-1;
				}

			uchar * const p=par.output_buf +
					par.dest_bytes_per_pixel * (x + y*par.working_x_size);

			for (uint k=0;k < 3;k++)
				if (k != c)
					p[k]=0;
			}
		}
	}

void interactive_image_processor_t::do_fullres_processing(
							const params_t par,const char * const fname)
{
	const vec<uint> image_size=get_image_size(&par);

	uchar * const buf=new uchar[image_size.x*image_size.y*3];

	processing_phase1_t phase1(image_reader,par.undo_enh_shadows);
	const color_and_levels_processing_t pass2(par.color_and_levels_params);

	phase1.skip_lines(par.top_crop);

	for (uint y=0;y < image_size.y;y++) {
		phase1.get_line();
		pass2.process_pixels(buf + y*image_size.x*3,
					phase1.output_line + 3*par.left_crop,image_size.x,1);
		}

	Magick::Image output_img(image_size.x,image_size.y,
												"BGR",Magick::CharPixel,buf);
	output_img.depth(8);
	output_img.write(fname);
	}

vec<uint> interactive_image_processor_t::get_image_size(const params_t *par)
{
	if (par == NULL)
		par=&params;

	QMutexLocker req(&image_load_mutex);

	const sint xsize=((sint)image_reader.img.columns()) -
									(sint)(par->left_crop + par->right_crop);
	const sint ysize=((sint)image_reader.img.rows()) -
									(sint)(par->top_crop + par->bottom_crop);
	const vec<uint> image_size={
			(uint)((xsize >= 1) ? xsize : 1),
			(uint)((ysize >= 1) ? ysize : 1)};
	return image_size;
	}

void interactive_image_processor_t::get_spot_values(
							const float x_fraction,const float y_fraction,
													uint values_in_file[3])
{
	QMutexLocker req(&image_load_mutex);
	image_reader.get_spot_values(x_fraction,y_fraction,values_in_file);
	}

image_reader_t::shooting_info_t interactive_image_processor_t::
												get_shooting_info(void)
{
	QMutexLocker req(&image_load_mutex);
	return image_reader.shooting_info;
	}
