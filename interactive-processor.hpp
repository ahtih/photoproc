/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <vec.hpp>
#include <tasking.hpp>

class interactive_image_processor_t : private BackgroundWorkerThread {
	public:

	enum operation_type_t {LOAD_FILE=0,PROCESSING,FULLRES_PROCESSING};

	struct notification_receiver_t {
		virtual void operation_completed(void)=0;
			// called in interactive_image_processor_t's thread
		};

	private:

	notification_receiver_t * const notification_receiver;
	Mutex image_load_mutex;
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
		} params;

	struct cmd_packet_t {
		operation_type_t operation_type;
		params_t params;
		void *param_ptr;
		filename fname;
		};

	struct result_t {
		operation_type_t operation_type;
		char *error_text;
		};

	void ensure_processing_level(const required_level_t level);
	virtual void Process(void *ptr,uint len);
	void do_processing(const params_t par);
	void do_fullres_processing(const params_t par,const char * const fname);
	void draw_processing_curve(const params_t par) const;
	void draw_gamma_test_image(const params_t par) const;
	static void resize_line(ushort *dest_p,const uint dest_size,
							const uint *src_p,const uint src_size);
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

	void start_operation(const operation_type_t operation_type,
				const char * const fname=NULL,void * const param_ptr=NULL);
	uint get_operation_results(operation_type_t &operation_type,
														char * &error_text);
			// returns 0 if no operation results are available
			// error_text has to be delete []'d by caller
	vec<uint> get_image_size(const params_t *par=NULL);
	void get_spot_values(const float x_fraction,const float y_fraction,
													uint values_in_file[3]);
	image_reader_t::shooting_info_t get_shooting_info(void);
	};
