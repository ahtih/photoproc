#include <stdlib.h>
#include <float.h>
#include "vec.hpp"
#include "color-patches-detector.hpp"

#define BASEGRID_RESOLUTION_MULTIPLIER	8
#define SMALL_SQUARE_SIZE			(BASEGRID_RESOLUTION_MULTIPLIER*45/100)

color_patches_detector_t::color_summer_t
	color_patches_detector_t::analyze_line(const vec<uint> &start_coords,
			const uint end_coord,uint vec<uint>::* const direction) const
{
	color_summer_t dest;
	const basegrid_elem_t *b=&basegrid[start_coords.x +
											start_coords.y * coord_to_idx.y];

	for (uint i=start_coords.*direction;i <= end_coord;
										i++,b+=coord_to_idx.*direction) {
		if (b->patch != NULL)
			return color_summer_t();
		dest+=*b;
		}

	return dest;
	}

void color_patches_detector_t::try_extend_patch(
						color_summer_t &color_summer,
						vec<uint> &min_coords,vec<uint> &max_coords,
						uint &another_patch_encountered_flag,
						uint vec<uint>::* const direction,
						uint vec<uint>::* const other_direction,
						const float max_variance_to_accept,
						const float min_color_sum_to_accept) const
{
	color_summer_t color_summer1,color_summer2;

	if (min_coords.*direction) {
		vec<uint> start_coords=min_coords;
		(start_coords.*direction)--;
		color_summer1=analyze_line(start_coords,
								max_coords.*other_direction,other_direction);
		}

	if (max_coords.*direction < basegrid_size.*direction-1) {
		vec<uint> start_coords=min_coords;
		start_coords.*direction=max_coords.*direction + 1;
		color_summer2=analyze_line(start_coords,
								max_coords.*other_direction,other_direction);
		}

	if (color_summer1.is_set() && color_summer2.is_set()) {
		if (color_summer1.variance() > color_summer2.variance())
			color_summer1=color_summer_t();
		  else
			color_summer2=color_summer_t();
		}
	  else
		another_patch_encountered_flag=1;

	color_summer_t new_color_summer=color_summer;
	new_color_summer+=color_summer1;
	new_color_summer+=color_summer2;

	if (new_color_summer.variance() > max_variance_to_accept ||
			color_sum(new_color_summer.min_color) < min_color_sum_to_accept)
		return;

	if (color_summer1.is_set())
		(min_coords.*direction)--;
	if (color_summer2.is_set())
		(max_coords.*direction)++;

	color_summer=new_color_summer;
	}

color_patches_detector_t::grid_calculator_t::grid_calculator_t(
			color_patches_detector_t * const _parent) : parent(_parent) {}

void color_patches_detector_t::grid_calculator_t::loop_over_patches(
				const vec<float> diagcorner1,const vec<float> sidecorner1,
				const vec<float> diagcorner2,const vec<float> sidecorner2)
{
	const vec<float> ystep1=(diagcorner1 - sidecorner1) *
								parent->nr_of_patches_minus1_reciprocal.y;
	const vec<float> ystep2=(sidecorner2 - diagcorner2) *
								parent->nr_of_patches_minus1_reciprocal.y;
	vec<float> y1=sidecorner1;
	vec<float> y2=diagcorner2;

	for (uint y=0;y < parent->nr_of_patches.y;y++,y1+=ystep1,y2+=ystep2) {
		const vec<float> xstep=(y2-y1) *
								parent->nr_of_patches_minus1_reciprocal.x;

		vec<float> coords=y1;
		for (uint xcount=parent->nr_of_patches.x;xcount--;coords+=xstep)
			process_patch(parent->basegrid +
						((uint)floor(coords.x+0.5)) +
						((uint)floor(coords.y+0.5))*parent->coord_to_idx.y,
					coords);
		}			
	}

color_patches_detector_t::grid_evaluator_t::grid_evaluator_t(
	color_patches_detector_t * const _parent) : grid_calculator_t(_parent) {}

void color_patches_detector_t::grid_evaluator_t::process_patch(
					basegrid_elem_t * const b,const vec<float> ideal_coords)
{
	if (b->patch == NULL) {
		variance_sum+=b->variance();
		anomalies++;
		}
	  else {
		if (b->patch->last_used_serial == parent->cur_serial)
			anomalies++;

		b->patch->last_used_serial=parent->cur_serial;
		variance_sum+=b->patch->variance();
		distance_error_sum+=(ideal_coords - b->patch->center).len_square();
		bounded_count+=b->patch->is_bounded_by_another_patch;
		}
	}

float color_patches_detector_t::grid_evaluator_t::calc_grid_error(
				const vec<float> diagcorner1,const vec<float> sidecorner1,
				const vec<float> diagcorner2,const vec<float> sidecorner2)
{
	parent->cur_serial++;

	distance_error_sum=variance_sum=0;
	anomalies=bounded_count=0;

	loop_over_patches(diagcorner1,sidecorner1,diagcorner2,sidecorner2);

	if (bounded_count < parent->nr_of_patches_desired/3)
		anomalies+=bounded_count;

	distance_error_sum /= parent->nr_of_patches_desired *
		BASEGRID_RESOLUTION_MULTIPLIER*BASEGRID_RESOLUTION_MULTIPLIER / 9.0;

	const float error_amount=distance_error_sum + (10*anomalies +
				variance_sum / parent->norm_patch_variance) /
											parent->nr_of_patches_desired;

	if (!parent->grids_evaluated || error_amount < 3.2)		//!!!
		printf("0x%08x error %g (%u bounded, %u anomalies, %g variance, %g distsquare)\n",
			parent->cur_serial,error_amount,bounded_count,anomalies,
			(variance_sum / parent->norm_patch_variance) /
											parent->nr_of_patches_desired,
			distance_error_sum);

	return error_amount;
	}

void color_patches_detector_t::evaluate_grid(const vec<float> diagcorner1,
						const vec<float> sidecorner1,
						const vec<float> diagcorner2,
						const vec<float> sidecorner2)
{
	grid_evaluator_t grid_evaluator(this);
	const float error_amount=grid_evaluator.calc_grid_error(
							diagcorner1,sidecorner1,diagcorner2,sidecorner2);

	grids_evaluated++;

	if (best_error_amount > error_amount) {
		best_error_amount = error_amount;
		best_diagcorner1=diagcorner1;
		best_sidecorner1=sidecorner1;
		best_diagcorner2=diagcorner2;
		best_sidecorner2=sidecorner2;
		}
	}

color_patches_detector_t::grid_color_picker_t::grid_color_picker_t(
	color_patches_detector_t * const _parent) : grid_calculator_t(_parent) {}

void color_patches_detector_t::grid_color_picker_t::process_patch(
								basegrid_elem_t * const b,const vec<float>)
{
	*(output_ptr++)=*((b->patch != NULL) ?
							static_cast<const color_summer_t *>(b->patch) :
							static_cast<const color_summer_t *>(b));
	}

void color_patches_detector_t::grid_color_picker_t::pick_colors(
							Magick::PixelPacket * const dest,
							vec<float> diagcorner1,vec<float> sidecorner1,
							vec<float> diagcorner2,vec<float> sidecorner2)
{
	output_ptr=dest;

	if (diagcorner2.y < diagcorner1.y)
		loop_over_patches(diagcorner1,sidecorner1,diagcorner2,sidecorner2);
	  else
		loop_over_patches(sidecorner1,diagcorner1,sidecorner2,diagcorner2);
	}

void color_patches_detector_t::draw_circle(Magick::Image &img,
										const vec<float> &basegrid_coords)
{
	const vec<uint> coords={
		(uint)floor(basegrid_coords.x*img.columns() / basegrid_size.x + 0.5),
		(uint)floor(basegrid_coords.y*img.   rows() / basegrid_size.y + 0.5)};

	img.draw(Magick::DrawableCircle(coords.x,coords.y,
				coords.x+(img.columns()/basegrid_size.x/2 + 1),coords.y));
	}

float color_patches_detector_t::detect(const Magick::Image &src_img,
			const vec<uint> _nr_of_patches,Magick::PixelPacket * const dest)
{
	nr_of_patches=_nr_of_patches;
	nr_of_patches_minus1_reciprocal.x=1.0 / (nr_of_patches.x-1);
	nr_of_patches_minus1_reciprocal.y=1.0 / (nr_of_patches.y-1);
	nr_of_patches_desired=nr_of_patches.x * nr_of_patches.y;

		/********************************************************/
		/*****                                              *****/
		/***** Downsample src_image into a smaller basegrid *****/
		/*****                                              *****/
		/********************************************************/

	basegrid_size=nr_of_patches * (uint)(BASEGRID_RESOLUTION_MULTIPLIER);
	if (basegrid_size.x > src_img.columns() ||
										basegrid_size.y > src_img.rows())
		return -1.0f;

	coord_to_idx.x=1;
	coord_to_idx.y=basegrid_size.x;

	const uint total_elems=basegrid_size.x * basegrid_size.y;
	basegrid=new basegrid_elem_t[total_elems];

	{ basegrid_elem_t *b=basegrid;
	uint src_y=0;
	for (uint y=0;y < basegrid_size.y;y++) {
		const uint next_src_y=(y+1)*src_img.rows() / basegrid_size.y;
		const Magick::PixelPacket * const src_line=src_img.getConstPixels(
								0,src_y,src_img.columns(),next_src_y-src_y);

		uint src_x=0;
		for (uint x=0;x < basegrid_size.x;x++,b++) {
			const uint next_src_x=(x+1)*src_img.columns() / basegrid_size.x;

			for (uint yy=0;yy < next_src_y-src_y;yy++) {
				const Magick::PixelPacket *p=src_line +
											src_x + yy*src_img.columns();
				const Magick::PixelPacket * const end_p=p + next_src_x-src_x;
				for (;p < end_p;p++)
					(*b)+=*p;
				}

			src_x=next_src_x;
			}

		src_y=next_src_y;
		}}

		/*******************************/
		/*****                     *****/
		/***** Set small_squares[] *****/
		/*****                     *****/
		/*******************************/

	const uint nr_of_small_squares=	(basegrid_size.y-(SMALL_SQUARE_SIZE-1)) *
									(basegrid_size.x-(SMALL_SQUARE_SIZE-1));
	small_square_t * const small_squares=
									new small_square_t[nr_of_small_squares];

	{ small_square_t *s=small_squares;
	for (uint y=0;y < basegrid_size.y-(SMALL_SQUARE_SIZE-1);y++) {
		basegrid_elem_t *b=basegrid + y*coord_to_idx.y;
		for (uint x=0;x < basegrid_size.x-(SMALL_SQUARE_SIZE-1);x++,b++,s++) {

			s->coords.x=x;
			s->coords.y=y;

			basegrid_elem_t *bb=b;
			for (uint yy=0;yy < SMALL_SQUARE_SIZE;
								yy++,bb+=coord_to_idx.y-SMALL_SQUARE_SIZE)
				for (uint xx=0;xx < SMALL_SQUARE_SIZE;xx++,bb++)
					(*s)+=*bb;

			// if (x < 32) printf("%2.0f ",s->variance()/2000);
			}

		// printf("\n");
		}}

	qsort(small_squares,nr_of_small_squares,sizeof(*small_squares),
										small_square_t::qsort_compare_func);

		/*******************************************************/
		/*****                                             *****/
		/***** Find homogenous-colored rectangular patches *****/
		/*****                                             *****/
		/*******************************************************/

	Magick::Image output_img=src_img;	//!!!
	output_img.fillColor("red");
	output_img.strokeColor("black");

	patch_t * const patches=new patch_t[nr_of_small_squares];

	cur_serial=0;
	norm_patch_variance=0;

	uint patches_found=0;
	{ for (uint start_elem_nr=0;start_elem_nr < nr_of_small_squares;
														start_elem_nr++) {
		const small_square_t * const s=&small_squares[start_elem_nr];

			/*****************************************************/
			/*****                                           *****/
			/***** check if starting square is already taken *****/
			/*****                                           *****/
			/*****************************************************/

		{ uint is_taken=0;
		basegrid_elem_t *bb=basegrid + s->coords.x +
												s->coords.y*coord_to_idx.y;
		for (uint yy=0;yy < SMALL_SQUARE_SIZE;
								yy++,bb+=coord_to_idx.y-SMALL_SQUARE_SIZE)
			for (uint xx=0;xx < SMALL_SQUARE_SIZE;xx++,bb++)
				if (bb->patch != NULL)
					is_taken=1;

		if (is_taken)
			continue; }

			/**************************************************/
			/*****                                        *****/
			/***** extend current patch as much as we can *****/
			/*****                                        *****/
			/**************************************************/

		vec<uint> min_coords=s->coords;
		vec<uint> max_coords={	min_coords.x+SMALL_SQUARE_SIZE-1,
								min_coords.y+SMALL_SQUARE_SIZE-1};

		color_summer_t color_summer=*s;
		uint another_patch_encountered_flag=0;

		{ const float max_variance_to_accept=s->variance() * 1.5;
		const float min_color_sum_to_accept=
							color_sum(color_summer.min_color) * 0.8;
		for (uint i=SMALL_SQUARE_SIZE;i < BASEGRID_RESOLUTION_MULTIPLIER;i++) {
			try_extend_patch(color_summer,min_coords,max_coords,
						another_patch_encountered_flag,
						&vec<uint>::x,&vec<uint>::y,
						max_variance_to_accept,min_color_sum_to_accept);
			try_extend_patch(color_summer,min_coords,max_coords,
						another_patch_encountered_flag,
						&vec<uint>::y,&vec<uint>::x,
						max_variance_to_accept,min_color_sum_to_accept);
			}}

			/***********************************/
			/*****                         *****/
			/***** mark the patch as taken *****/
			/*****                         *****/
			/***********************************/

		{ for (uint y=min_coords.y;y <= max_coords.y;y++)
			for (uint x=min_coords.x;x <= max_coords.x;x++) {
				patch_t * &patch=basegrid[x + y*coord_to_idx.y].patch;

				if (patch != NULL) abort();
				patch=patches + patches_found;
				}}

		patches[patches_found]+=color_summer;
		patches[patches_found].center.x=(min_coords.x + max_coords.x) / 2.0;
		patches[patches_found].center.y=(min_coords.y + max_coords.y) / 2.0;
		patches[patches_found].is_bounded_by_another_patch=
											another_patch_encountered_flag;
		patches[patches_found].last_used_serial=cur_serial;
		patches_found++;

		if (patches_found < nr_of_patches_desired)
			norm_patch_variance+=color_summer.variance() /
													nr_of_patches_desired;

		output_img.draw(Magick::DrawableRectangle(		//!!!
				min_coords.x*src_img.columns() / basegrid_size.x,
				min_coords.y*src_img.   rows() / basegrid_size.y,
				(max_coords.x+1)*src_img.columns() / basegrid_size.x - 1,
				(max_coords.y+1)*src_img.   rows() / basegrid_size.y - 1));
		}}

	delete [] small_squares;

		/******************************************/
		/*****                                *****/
		/***** Try to fit a grid onto patches *****/
		/*****                                *****/
		/******************************************/

	const patch_t * const end_patches=patches + patches_found;

	best_error_amount=FLT_MAX;
	grids_evaluated=0;

	/*
	{ const vec<float> c[4]={{14.5,103}, {15.5,14.5}, {93.5,15.5}, {92.5,104}};
	evaluate_grid(c[0],c[1],c[2],c[3]); }
	*/


	{ for (const patch_t * patch1=patches;patch1 < end_patches;patch1++)
		for (const patch_t * patch2=patches;patch2 < end_patches;patch2++) {

			const vec<float> diagonal_vec=patch2->center - patch1->center;
			if (diagonal_vec.x <= 0)
				continue;

			const float grid_area=fabs(diagonal_vec.x * diagonal_vec.y);
			if (grid_area < total_elems/4)
				continue;

			vec<float> ideal_sidecorner1=patch1->center;
			ideal_sidecorner1.y+=diagonal_vec.y;

			vec<float> ideal_sidecorner2=patch2->center;
			ideal_sidecorner2.y-=diagonal_vec.y;

			evaluate_grid(	patch1->center,ideal_sidecorner1,
							patch2->center,ideal_sidecorner2);

			const float middle_x=(patch1->center.x + patch2->center.x) / 2;

			const float max_sidecorner_distance=sqrt(grid_area) / 4;

			uint is_first_sidecorner1=1;
			for (const patch_t * sidecorner_patch1=patches;
						sidecorner_patch1 < end_patches;sidecorner_patch1++) {
				if (sidecorner_patch1->center.x >= middle_x ||
					sidecorner_patch1->center.manhattan_distance_to(
								ideal_sidecorner1) > max_sidecorner_distance)
					continue;

				evaluate_grid(	patch1->center,sidecorner_patch1->center,
								patch2->center,ideal_sidecorner2);

				{ const vec<float> calc_corner=
											patch2->center + patch1->center -
											sidecorner_patch1->center;
				if (calc_corner.x >= 0 && calc_corner.x < basegrid_size.x-1 &&
					calc_corner.y >= 0 && calc_corner.y < basegrid_size.y-1)
					evaluate_grid(	patch1->center,sidecorner_patch1->center,
									patch2->center,calc_corner); }

				for (const patch_t * sidecorner_patch2=patches;
						sidecorner_patch2 < end_patches;sidecorner_patch2++) {
					if (sidecorner_patch2->center.x <= middle_x ||
						sidecorner_patch2->center.manhattan_distance_to(
								ideal_sidecorner2) > max_sidecorner_distance)
						continue;

					if (is_first_sidecorner1) {
						evaluate_grid(	patch1->center,ideal_sidecorner1,
										patch2->center,
												sidecorner_patch2->center);
						{ const vec<float> calc_corner=
											patch1->center + patch2->center -
											sidecorner_patch2->center;
						if (calc_corner.x >= 0 &&
										calc_corner.x < basegrid_size.x-1 &&
							calc_corner.y >= 0 &&
										calc_corner.y < basegrid_size.y-1)
							evaluate_grid(	patch1->center,calc_corner,
											patch2->center,
												sidecorner_patch2->center); }
						}

					evaluate_grid(	patch1->center,sidecorner_patch1->center,
									patch2->center,sidecorner_patch2->center);
					}

				is_first_sidecorner1=0;
				}
			}}

	printf("%u grids evaluated, best error %g, corners "
								"%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f\n",
					grids_evaluated,best_error_amount,
					best_diagcorner1.x,best_diagcorner1.y,
					best_sidecorner1.x,best_sidecorner1.y,
					best_diagcorner2.x,best_diagcorner2.y,
					best_sidecorner2.x,best_sidecorner2.y);

	output_img.strokeWidth(0);
	output_img.fillColor("green");
	draw_circle(output_img,best_diagcorner1);
	draw_circle(output_img,best_diagcorner2);
	output_img.fillColor("blue");
	draw_circle(output_img,best_sidecorner1);
	draw_circle(output_img,best_sidecorner2);

	output_img.write("debug.bmp");	//!!!

	grid_color_picker_t grid_color_picker(this);
	grid_color_picker.pick_colors(dest,	best_diagcorner1,best_sidecorner1,
										best_diagcorner2,best_sidecorner2);

	delete [] patches;
	delete [] basegrid;
	return best_error_amount;
	}
