/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#define INCL_DOS
#include <stdio.h>
#include <v/vapp.h>
#include <v/vicon.h>
#include <v/vcmdwin.h>
#include <v/vcmdpane.h>
#include <v/vmenu.h>
#include <v/vcanvas.h>
#include <v/vmemdc.h>
#include <v/vfilesel.h>

#include <misc.hpp>
#include <file.hpp>
#include "processing.hpp"
#include "interactive-processor.hpp"

class cmd_window_t;

class image_canvas_t : public vCanvasPane
{
	cmd_window_t * const cmd_window;
	uint is_initial_size_known;		// 0 or 1

	static void clear_vdc(vDC * const _vdc);

	public:

	vMemoryDC *memory_dc;
	uint memory_dc_x_size,memory_dc_y_size;

	image_canvas_t(cmd_window_t * const _cmd_window);
	~image_canvas_t(void);
	uint update_memory_dc_size(void);
		// returns nonzero if size was changed
	virtual void Redraw(sint x=0,sint y=0,sint width=0,sint height=0);
	virtual void Resize(sint width,sint height);
	};

class cmd_window_t : public vCmdWindow,
			private interactive_image_processor_t::notification_receiver_t
{
	vMenuPane *menu_pane;
	image_canvas_t *canvas_pane;
	vCommandPane *command_pane;

	uchar *buf;		// always the same size as canvas_pane->memory_dc
	interactive_image_processor_t processor;

#ifdef __OS2__
	const HWND window_handle;
#endif

	enum id_t {OPERATION_COMPLETED=3,MENU_FILE,MENU_EXIT,MENU_SAVE,
				CHANGE_ENH_SHADOWS,
				BLACK_LEVEL_FRAME,CHANGE_BLACK_LEVEL,BLACK_LEVEL_DISPLAY,
				WHITE_LEVEL_FRAME,CHANGE_WHITE_LEVEL,WHITE_LEVEL_DISPLAY};

	static vMenu main_menu_def[];
	static vMenu file_menu_def[];
	static CommandObject command_bar[];

	void alloc_buf(void);
	void enable_disable_controls(void);
	void color_and_levels_params_changed(void);
	float process_level(const uint slider_id,const uint display_id);

	public:

	cmd_window_t(void);
	~cmd_window_t(void);
	virtual void WindowCommand(ItemVal id,ItemVal val,CmdType cType);

	virtual void operation_completed(void);
		// called in interactive_image_processor_t's thread

	void check_processing(void);
	void start_loading_file(const char * const fname);
	};

/***************************************************************************/
/****************************                  *****************************/
/**************************** image_canvas_t:: *****************************/
/****************************                  *****************************/
/***************************************************************************/

image_canvas_t::image_canvas_t(cmd_window_t * const _cmd_window) :
					cmd_window(_cmd_window), is_initial_size_known(0),
					memory_dc(new vMemoryDC(1,1)),
					memory_dc_x_size(1), memory_dc_y_size(1)
{
	clear_vdc(memory_dc);
	}

image_canvas_t::~image_canvas_t(void)
{
	if (memory_dc != NULL)
		delete memory_dc;
	}

void image_canvas_t::clear_vdc(vDC * const _vdc)
{
	vColor color(0x60,0x60,0x60);
	_vdc->SetBackground(color);
	_vdc->Clear();
	}

uint image_canvas_t::update_memory_dc_size(void)
{						// returns nonzero if size was changed

	const uint x_size=GetWidth();
	const uint y_size=GetHeight();

	if (!x_size || !y_size)
		return 0;

	is_initial_size_known=1;

	if (memory_dc_x_size == x_size && memory_dc_y_size == y_size)
		return 0;

	if (memory_dc != NULL)
		delete memory_dc;

	memory_dc_x_size=x_size;
	memory_dc_y_size=y_size;
	memory_dc=new vMemoryDC(memory_dc_x_size,memory_dc_y_size);
	clear_vdc(memory_dc);
	return 1;
	}

void image_canvas_t::Redraw(sint,sint,sint width,sint height)
{
	if (!width || !height)
		return;

	const uint x_size=GetWidth();
	const uint y_size=GetHeight();

	if (!x_size || !y_size)
		return;

	if (!is_initial_size_known)
		cmd_window->check_processing();

	if (memory_dc_x_size == x_size && memory_dc_y_size == y_size) {
		CopyFromMemoryDC(memory_dc,0,0);
		}
	  else
		clear_vdc(GetDC());
	}

void image_canvas_t::Resize(sint,sint)
{
	cmd_window->check_processing();
	Redraw(0,0,1,1);
	}

#if 0
#ifdef USE_DrawColorPoints
	vColor * const points=new vColor[x_size+1];

	for (uint y=0;y < y_size && y < img.Height;y++) {

		const uchar *p=&((const uchar *)img.Buf)[y * img.Width * 3];

		uint x;
		for (x=0;x < x_size && x < img.Width;x++,p+=3)
			points[x].Set(p[0],p[1],p[2]);

		_vdc->DrawColorPoints(0,y,x,points);
		}

	delete [] points;
#else
	/*
	const HPS _hps=memory_dc->GetHPS();

	const uint row_words=((x_size*3) + 3) / 4;

	uchar * const buf=new uchar[row_words * 4];
    PBITMAPINFO2 pbmi;

    GpiSetBitmapBits(_hps,0,y_size,(PBYTE)bmbits,pbmi);
	delete [] buf;
	*/

	img.Resize(x_size,y_size);

	const uint nr_of_pixels=img.Width * img.Height;
	const uchar *p=(const uchar *)img.Buf;
	uchar *q=icon_buf;
	for (uint i=0;i < nr_of_pixels;i++,p+=3,q+=3) {
		q[2]=min(p[0],param_value);
		q[1]=min(p[1],param_value);
		q[0]=min(p[2],param_value);
		}

	vIcon icon(icon_buf,y_size,x_size,24);
	_vdc->DrawIcon(0,0,icon);
#endif
#endif

/***************************************************************************/
/*****************************                ******************************/
/***************************** cmd_window_t:: ******************************/
/*****************************                ******************************/
/***************************************************************************/

vMenu cmd_window_t::main_menu_def[]={
	{"&File", MENU_FILE, isSens, notUsed, notUsed, noKey, file_menu_def},
	{NULL} };

vMenu cmd_window_t::file_menu_def[]={
	{"&Save as...", MENU_SAVE, isSens, notChk, noKeyLbl, noKey, noSub},
	{"&Exit", 		MENU_EXIT, isSens, notChk, noKeyLbl, noKey, noSub},
	{NULL} };

CommandObject cmd_window_t::command_bar[]={
	{C_Frame,BLACK_LEVEL_FRAME,0,"",NoList,CA_None,isSens,NoFrame,0,0,0,""},
	{C_Slider,CHANGE_BLACK_LEVEL,0 /* 60 * 100/255 */,"",NoList,CA_Horizontal,
										isSens,BLACK_LEVEL_FRAME,0,0,0,""},
	{C_Text,BLACK_LEVEL_DISPLAY,0,"","255",CA_NoBorder,isSens,BLACK_LEVEL_FRAME,
										CHANGE_BLACK_LEVEL,0,0,""},


	{C_Frame,WHITE_LEVEL_FRAME,0,"",NoList,CA_None,isSens,NoFrame,0,0,0,""},
	{C_Slider,CHANGE_WHITE_LEVEL,100,"",NoList,CA_Horizontal,
										isSens,WHITE_LEVEL_FRAME,0,0,0,""},
	{C_Text,WHITE_LEVEL_DISPLAY,0,"","255",CA_NoBorder,isSens,WHITE_LEVEL_FRAME,
										CHANGE_WHITE_LEVEL,0,0,""},

	{C_CheckBox,CHANGE_ENH_SHADOWS,1,"Undo Enh Shadows",NoList,CA_None,
												isSens,NoFrame,0,0,0,""},

	{C_EndOfList,0,0,0,0,CA_None,0,0,0} };

cmd_window_t::cmd_window_t(void) : vCmdWindow(""
#ifdef __OS2__
												,10,10),
#endif
								processor(this), window_handle(winHwnd())
{
	menu_pane=new vMenuPane(main_menu_def);
	AddPane(menu_pane);

	command_pane=new vCommandPane(command_bar);
	AddPane(command_pane);

	canvas_pane=new image_canvas_t(this);
	AddPane(canvas_pane);

#ifdef __OS2__
	WinSetWindowPos(window_handle,0,0,0,0,0,SWP_MAXIMIZE);
	// canvas_pane->SetWidthHeight(37*12,37*12 *3/2);		//!!!
#endif

	alloc_buf();
	processor.set_enh_shadows(!!GetValue(CHANGE_ENH_SHADOWS));
	color_and_levels_params_changed();
	enable_disable_controls();

	ShowWindow();
	}

cmd_window_t::~cmd_window_t(void)
{
	delete canvas_pane;
	delete [] buf;

	delete menu_pane;
	delete command_pane;
	}

void cmd_window_t::alloc_buf(void)
{
	buf=new uchar [	canvas_pane->memory_dc_x_size *
					canvas_pane->memory_dc_y_size * 3];
	processor.set_working_res(	canvas_pane->memory_dc_x_size,
								canvas_pane->memory_dc_y_size,buf);
	}

void cmd_window_t::enable_disable_controls(void)
{
	SetTitle(processor.operation_pending_count ? "processing..." : "idle");	//!!!
	}

void cmd_window_t::operation_completed(void)
{			// called in interactive_image_processor_t's thread

#ifdef __OS2__
	WinPostMsg(window_handle,WM_COMMAND,MPFROMSHORT(OPERATION_COMPLETED),
										MPFROM2SHORT(CMDSRC_OTHER,FALSE));
#else
#error OS not supported
#endif
	}

void cmd_window_t::check_processing(void)
{
	if (processor.operation_pending_count)
		return;

	if (canvas_pane->update_memory_dc_size()) {
		delete [] buf;
		alloc_buf();
		}

	if (processor.is_processing_necessary && processor.is_file_loaded)
		processor.start_operation(interactive_image_processor_t::PROCESSING);

	enable_disable_controls();
	}

void cmd_window_t::WindowCommand(ItemVal id,ItemVal val,CmdType)
{
	if (id == OPERATION_COMPLETED) {
		interactive_image_processor_t::operation_type_t operation_type;
		char *error_text;
		while (processor.get_operation_results(operation_type,error_text)) {
			if (error_text != NULL) {
				//!!!
				delete [] error_text;
				continue;
				}

			if (operation_type == interactive_image_processor_t::PROCESSING) {
				vIcon icon(buf,	canvas_pane->memory_dc_y_size,
								canvas_pane->memory_dc_x_size,24);
				canvas_pane->memory_dc->DrawIcon(0,0,icon);
				canvas_pane->Redraw(0,0,1,1);
				}
			}

		check_processing();
		return;
		}

	switch (id) {
		case CHANGE_ENH_SHADOWS:
			processor.set_enh_shadows(val);
			check_processing();
			break;

		case CHANGE_WHITE_LEVEL:
		case CHANGE_BLACK_LEVEL:
			color_and_levels_params_changed();
			break;

		case MENU_SAVE:
			/*
				if (!file_save_enabled)
					break;
				*/

			{vFileSelect fsel(this);
			static char *filter_list[]={"*.bmp", "*", NULL};
			filename fname;
			sint filter_index=0;
			if (!fsel.FileSelectSave("Save As...",
										fname.Name,sizeof(fname.Name)-1,
										filter_list,filter_index))
				break;

			/*
				if (!file_save_enabled)
					break;
				*/

			if (!*(const char *)fname)
				break;

			DbgPrintf("Saving as %s\n",(const char *)fname);

			processor.start_operation(
						interactive_image_processor_t::FULLRES_PROCESSING,
														(const char *)fname);
			enable_disable_controls();
			} break;
		}
	}

float cmd_window_t::process_level(const uint slider_id,const uint display_id)
{
	const uint val=(uint)GetValue(slider_id);

	char buf[20];
	sprintf(buf,"%u",val * 255 / 100);
	SetString(display_id,buf);

	return pow(val / 100.0,2.2);
	}

void cmd_window_t::color_and_levels_params_changed(void)
{
	color_and_levels_processing_t::params_t params;

	params.black_level=process_level(CHANGE_BLACK_LEVEL,BLACK_LEVEL_DISPLAY);
	params.white_level=process_level(CHANGE_WHITE_LEVEL,WHITE_LEVEL_DISPLAY);

	processor.set_color_and_levels_params(params);
	check_processing();
	}

void cmd_window_t::start_loading_file(const char * const fname)
{
	/*
		if (!file_load_enabled)
			return;
		*/

	processor.start_operation(interactive_image_processor_t::LOAD_FILE,fname);
	enable_disable_controls();
	}

/***************************************************************************/
/*********************************         *********************************/
/********************************* app_t:: *********************************/
/*********************************         *********************************/
/***************************************************************************/

class app_t : public vApp {
	public:
	app_t(char * const name) : vApp(name,1) {}

	virtual vWindow *NewAppWin(vWindow *win,char *name,sint w,sint h,
														vAppWinInfo *awinfo);
	};

vWindow* app_t::NewAppWin(vWindow *win,char *name,sint w,sint h,
														vAppWinInfo *awinfo)
{
	if (win == NULL)
		win=new cmd_window_t();

	return vApp::NewAppWin(win,name,w,h,awinfo);
	}

static app_t app("imageproc");

char dummy_function_to_remove_warnings(void)
{
#undef PI
	return char(PI + (uint)&szvFrameClass + (uint)&szvWindowClass);
	}

sint AppMain(sint argc,char **argv)
{
	if (file::Exists("DbgPrintf.log"))
		Debug.OpenLog("DbgPrintf.log");

	cmd_window_t * const cmd_window=(cmd_window_t *)
									theApp->NewAppWin(NULL,"",0,0,NULL);
	if (argc > 1) {
		theApp->CheckEvents();		// make sure events done before opening
		cmd_window->start_loading_file(argv[1]);
		}

    return 0;
	}
