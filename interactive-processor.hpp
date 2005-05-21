/* Copyright (C) 2003-2005 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <qmutex.h>
#include <qthread.h>

class SyncQueue {

	struct Element {
		Element *Next;
		uint Len;
		
		void *DataPtr(void){return ((char*)this-Len);}
	};
	Element *Head,*Tail;

	QMutex mutex;
	QWaitCondition cond;

	public:
	SyncQueue(void);
	~SyncQueue(void);
	void Write(const void *ptr,uint len);
	void *Read(uint &len,const uint no_wait=0);
	void Release(void *ptr) {delete [] (char *)ptr;}
};

class interactive_image_processor_t : public QThread, public SyncQueue {
	public:

	enum operation_type_t {LOAD_FILE=0,LOAD_FROM_MEMORY,
			LOAD_FROM_MEMORY_AND_DELETE_FILE,PROCESSING,FULLRES_PROCESSING};
	struct notification_receiver_t {
		virtual void operation_completed(void)=0;
			// called in interactive_image_processor_t's thread
		};

	private:

	notification_receiver_t * const notification_receiver;
	QMutex image_load_mutex;
	image_reader_t image_reader;
	ushort *lowres_phase1_image;		// 2.0-gamma RGB ushort's
	SyncQueue results_queue;

	enum required_level_t {PASS2=0,PASS1,NEW_LOWRES_BUF};

	struct params_t {
		required_level_t required_level;
		uchar *output_buf;
		uint output_in_BGR_format;	// 0 or 1
		uint dest_bytes_per_pixel;	// usually 3 or 4
		uint working_x_size,working_y_size;
		uint undo_enh_shadows;
		color_and_levels_processing_t::params_t color_and_levels_params;
		uint top_crop,bottom_crop,left_crop,right_crop;
		vec<uint> fullres_resize_size;		// .x==0 if no resize
		float unsharp_mask_radius;			// <=0 if no unsharp mask
		} params;

	struct cmd_packet_t {
		operation_type_t operation_type;
		params_t params;
		void *param_ptr;
		uint param_uint;
		char fname[300];
		};

	struct result_t {
		operation_type_t operation_type;
		char *error_text;
		};

	void ensure_processing_level(const required_level_t level);

	virtual void run(void);

	void do_processing(const params_t par);
	void do_fullres_processing(const params_t par,const char * const fname);
	void draw_processing_curve(const params_t par) const;
	void draw_gamma_test_image(const params_t par) const;
	static void resize_line(ushort *dest_p,const uint dest_size,
							const uint *src_p,const uint src_size);
	vec<float> get_full_frame_pos_fraction(const vec<float> pos_fraction);
	public:

	uint operation_pending_count;	// 0..
	uint is_processing_necessary;	// 0 or 1
	uint is_file_loaded;			// 0 or 1

	interactive_image_processor_t(
					notification_receiver_t * const _notification_receiver);
	~interactive_image_processor_t(void);

	void set_working_res(const uint x_size,const uint y_size,
				uchar * const output_buf,const uint output_in_BGR_format=0,
				const uint dest_bytes_per_pixel=3);
	void set_enh_shadows(const uint _undo_enh_shadows);
	void set_crop(	const uint top_pixels,const uint bottom_pixels,
					const uint left_pixels,const uint right_pixels);
	void set_color_and_levels_params(
					const color_and_levels_processing_t::params_t &_params);
	void set_fullres_processing_params(
			const vec<uint> &resize_size /* .x==0 if no resize */,
			const float unsharp_mask_radius=-1.0f /* <=0 if no unsharp mask */);

	void start_operation(const operation_type_t operation_type,
				const char * const fname=NULL,void * const param_ptr=NULL,
				const uint param_uint=0);
	uint get_operation_results(operation_type_t &operation_type,
														char * &error_text);
			// returns 0 if no operation results are available
			// error_text has to be delete []'d by caller
	vec<uint> get_image_size(const params_t *par=NULL);
	void get_spot_values(const vec<float> pos_fraction,
													uint values_in_file[3]);
	uint get_rectilinear_angles(const vec<float> pos_fraction,
										vec<float> &dest_angles_in_degrees);
			// returns nonzero and sets dest if angles can be calculated;
			//   otherwise returns 0
	image_reader_t::shooting_info_t get_shooting_info(void);
	};
