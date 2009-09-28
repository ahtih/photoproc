#include <Magick++.h>

class color_patches_detector_t {

	inline static float color_sum(const Magick::PixelPacket &pp)
		{ return pp.red + pp.green + pp.blue; }

	struct color_summer_t {
		Magick::PixelPacket min_color,max_color;

		double r,g,b;
		uint count;

		color_summer_t(void) : min_color(Magick::Color(
									Magick::Color::scaleDoubleToQuantum(1),
									Magick::Color::scaleDoubleToQuantum(1),
									Magick::Color::scaleDoubleToQuantum(1))),
							max_color(Magick::Color(0,0,0)),
							r(0), g(0), b(0), count(0) {}

		inline uint is_set(void) const
			{ return max_color.red >= min_color.red; }

		inline float variance(void) const
			{
				return color_sum(max_color) - color_sum(min_color);
				}

		private:

		inline void apply_min_color(const Magick::PixelPacket &c)
			{
				if (min_color.red > c.red)
					min_color.red = c.red;
				if (min_color.green > c.green)
					min_color.green = c.green;
				if (min_color.blue > c.blue)
					min_color.blue = c.blue;
				}

		inline void apply_max_color(const Magick::PixelPacket &c)
			{
				if (max_color.red < c.red)
					max_color.red = c.red;
				if (max_color.green < c.green)
					max_color.green = c.green;
				if (max_color.blue < c.blue)
					max_color.blue = c.blue;
				}
		public:

		inline void operator += (const Magick::PixelPacket &c)
			{
				apply_min_color(c);
				apply_max_color(c);

				r+=c.red;
				g+=c.green;
				b+=c.blue;
				count++;
				}

		inline void operator += (const color_summer_t &c)
			{
				apply_min_color(c.min_color);
				apply_max_color(c.max_color);

				r+=c.r;
				g+=c.g;
				b+=c.b;
				count+=c.count;
				}

		operator Magick::PixelPacket (void) const
			{
				return Magick::Color(	(Magick::Quantum)(r/count),
										(Magick::Quantum)(g/count),
										(Magick::Quantum)(b/count));
				}
		};

	struct patch_t : color_summer_t {
		vec<float> center;
		uint is_bounded_by_another_patch;
		uint last_used_serial;
		};

	struct basegrid_elem_t : color_summer_t {
		patch_t *patch;		// NULL if not part of any patch

		basegrid_elem_t(void) : patch(NULL) {}
		};

	struct small_square_t : color_summer_t {
		vec<uint> coords;

		static sint qsort_compare_func(	const void * const a,
										const void * const b)
			{
				const double diff=	((const small_square_t *)a)->variance() -
									((const small_square_t *)b)->variance();
				if (diff < 0)
					return -1;
				if (diff > 0)
					return +1;
				return 0;
				}
		};

	class grid_calculator_t {
		protected:
		color_patches_detector_t * const parent;

		public:

		grid_calculator_t(color_patches_detector_t * const _parent);
		void loop_over_patches(
				const vec<float> diagcorner1,const vec<float> sidecorner1,
				const vec<float> diagcorner2,const vec<float> sidecorner2);
		virtual void process_patch(basegrid_elem_t * const b,
										const vec<float> ideal_coords)=0;
		};

	class grid_evaluator_t : protected grid_calculator_t {
		float distance_error_sum,variance_sum;
		uint anomalies,bounded_count;

		virtual void process_patch(basegrid_elem_t * const b,
											const vec<float> ideal_coords);
		public:

		grid_evaluator_t(color_patches_detector_t * const _parent);
		float calc_grid_error(
				const vec<float> diagcorner1,const vec<float> sidecorner1,
				const vec<float> diagcorner2,const vec<float> sidecorner2);
		};

	class grid_color_picker_t : protected grid_calculator_t {
		Magick::PixelPacket *output_ptr;
		virtual void process_patch(basegrid_elem_t * const b,
														const vec<float>);
		public:
		grid_color_picker_t(color_patches_detector_t * const _parent);
		void pick_colors(Magick::PixelPacket * const dest,
							vec<float> diagcorner1,vec<float> sidecorner1,
							vec<float> diagcorner2,vec<float> sidecorner2);
		};

	basegrid_elem_t *basegrid;
	vec<uint> nr_of_patches,basegrid_size,coord_to_idx;
	vec<float> nr_of_patches_minus1_reciprocal;
	uint nr_of_patches_desired;

	float norm_patch_variance;
	uint cur_serial;

	vec<float> best_diagcorner1,best_sidecorner1;
	vec<float> best_diagcorner2,best_sidecorner2;
	float best_error_amount;
	uint grids_evaluated;

	color_summer_t analyze_line(const vec<uint> &start_coords,
						const uint end_coord,
						uint vec<uint>::* const direction) const;

	void try_extend_patch(color_summer_t &color_summer,
						vec<uint> &min_coords,vec<uint> &max_coords,
						uint &another_patch_encountered_flag,
						uint vec<uint>::* const direction,
						uint vec<uint>::* const other_direction,
						const float max_variance_to_accept,
						const float min_color_sum_to_accept) const;

	void evaluate_grid(	const vec<float> diagcorner1,
						const vec<float> sidecorner1,
						const vec<float> diagcorner2,
						const vec<float> sidecorner2);

	void draw_circle(Magick::Image &img,const vec<float> &basegrid_coords);

	public:

	float detect(const Magick::Image &src_img,
			const vec<uint> nr_of_patches,Magick::PixelPacket * const dest);
	};
