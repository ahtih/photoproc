/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <qapplication.h>
#include <qpopupmenu.h>
#include <qmenubar.h>
#include <qmainwindow.h>
#include <qcolor.h>
#include <qpixmap.h>
#include <qlabel.h>
#include <qslider.h>
#include <qhbox.h>
#include <qvbox.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qcheckbox.h>
#include <qimage.h>
#include <qthread.h>
#include <qprocess.h>
#include <qfiledialog.h>
#include <qmessagebox.h>
#include <qsettings.h>

#include <misc.hpp>
#include <file.hpp>
#include "processing.hpp"
#include "interactive-processor.hpp"

#if MEASURE_BLIT_TIMES
#include <timer.hpp>
#endif

#define MESSAGE_BOX_CAPTION 		"photoproc"
#define SETTINGS_PREFIX				"/photoproc/"

#define NR_OF_IMAGES_TO_REMEMBER	20

class image_window_t;
class external_reader_process_t;

class image_widget_t : public QWidget {
    Q_OBJECT
	image_window_t * const image_window;
	QImage qimage;
	QPixmap qpixmap;

	void do_bitblt(void)
		{
			const uint xoff=( width()-qpixmap. width()) / 2;
			const uint yoff=(height()-qpixmap.height()) / 2;

			erase(QRegion(rect(),QRegion::Rectangle).subtract(
					QRegion(xoff,yoff,qpixmap.width(),qpixmap.height(),
											QRegion::Rectangle)));
			bitBlt(this,xoff,yoff,&qpixmap);
			}

	protected:

	virtual void paintEvent(QPaintEvent *) { do_bitblt(); }

	virtual void mousePressEvent(QMouseEvent *e);
	virtual void resizeEvent(QResizeEvent *);

	public:

	image_widget_t(QWidget * const parent,
									image_window_t * const _image_window) :
			QWidget(parent), image_window(_image_window), qimage(1,1,32)
			{ setEraseColor(black); }

	void ensure_correct_size(void);

	void refresh_image(void)
		{
#if MEASURE_BLIT_TIMES
			const uint tim=timer::get_ms();
#endif
			qpixmap.convertFromImage(qimage);
			do_bitblt();
#if MEASURE_BLIT_TIMES
			DbgPrintf("time %u ms\n",timer::get_ms()-tim);
#endif
			}
	};

class slider_t : public QHBox {
	Q_OBJECT

	const STR display_format;
	QLabel *value_display;

	public slots:

	void value_changed(void)
		{
			char buf[100];
			sprintf(buf,display_format,slider->value() / 100.0);
			value_display->setText(buf);
			}

	public:

	QSlider *slider;

	slider_t(QWidget * const parent,const char * const name,
					const float min_value,const float max_value,
					const float default_value,
					const char * const _display_format="%.2f") :
							QHBox(parent), display_format(_display_format)
		{
			setSpacing(5);
			new QLabel(QString(name) + ":",this);

			QFont font;
			QFontMetrics font_metrics(font);
			value_display=new QLabel("",this);
			value_display->setFont(font);

			char buf[100];
			sprintf(buf,display_format,(float)max_value - 1/100.0);
			value_display->setFixedWidth(font_metrics.width(buf));

			slider=new QSlider(	(sint)floor(min_value*100 + 0.5),
								(sint)floor(max_value*100 + 0.5),10,
								(sint)floor(default_value*100 + 0.5),
													Qt::Horizontal,this);

			connect(slider,SIGNAL(valueChanged(int)),SLOT(value_changed()));
			value_changed();
			}

	float get_value(void) const { return slider->value() / 100.0; }
	};

class crop_spin_box_t : public QHBox {
	Q_OBJECT

	public:

	QSpinBox *spinbox;

	crop_spin_box_t(QWidget * const parent,const char * const name) :
															QHBox(parent)
		{
			setSpacing(5);
			new QLabel(QString(name) + ":",this);
			spinbox=new QSpinBox(0,10*1000,20,this);
			}

	uint get_value(void) const { return (uint)spinbox->value(); }
	};

class image_window_t : public QMainWindow,
			private interactive_image_processor_t::notification_receiver_t {
	Q_OBJECT

	filename image_fname;
	IMAGE::FILESOURCE *image_load_filesource;	// NULL if no filesource load in progress
	external_reader_process_t *external_reader_process;	// NULL if no external_reader_process in progress

	QPopupMenu file_menu;
	image_widget_t *image_widget;

	QHBox *normal_view_hbox;
	slider_t *contrast_slider,*exposure_slider;
	slider_t *black_level_slider,*white_clipping_slider;

	QHBox *color_balance_view_hbox;
	slider_t *red_balance_slider,*blue_balance_slider;
	QCheckBox *grayscale_checkbox;

	QHBox *crop_view_hbox;
	QLabel *crop_info_qlabel;
	crop_spin_box_t *top_crop,*bottom_crop,*left_crop,*right_crop;

	virtual void operation_completed(void);
	void set_recent_images_in_file_menu(void);

	protected:

	virtual bool event(QEvent *e);

	public slots:

	void load_image(const QString &fname);
	void enable_disable_controls(void);
	void check_processing(void);
	void color_and_levels_params_changed(void);
	void crop_params_changed(void);

	void open_file_dialog(void)
		{
			const QString fname=QFileDialog::getOpenFileName(
				QString::null,"image files (*.bmp *.tif *.tiff *.psd *.CRW *.crw)",
				this,"open image dialog","Open image");
			if (fname.isNull())
				return;

			load_image(fname);
			}

	void save_as(void)
		{
			filename save_fname=image_fname;
			if (!*(const char *)save_fname)
				return;
			save_fname.SetExt("bmp");

			const QString fname=QFileDialog::getSaveFileName(
				save_fname.BaseName(),"BMP files (*.bmp)",
				this,"save as dialog","Save As");
			if (fname.isNull())
				return;

			DbgPrintf("Saving as %s\n",(const char *)fname);

			processor.start_operation(
					interactive_image_processor_t::FULLRES_PROCESSING,fname);
			enable_disable_controls();
			}

	void load_recent_image(int menuitem_id)
		{
			if (menuitem_id <= 0)
				return;

			QString menuitem_text=file_menu.text(menuitem_id);
			while (menuitem_text.length() && menuitem_text.at(0) != ' ')
				menuitem_text.remove(0,1);

			load_image(menuitem_text.stripWhiteSpace());
			}

	void load_last_image(void)
		{
			load_image(settings.readEntry(
							SETTINGS_PREFIX "recent_images/1"));
			}

	void open_next_numbered_image(void)
		{
			const char * const basename=image_fname.BaseName();
			if (basename == NULL)
				return;

			for (uint increment=1;increment < 100;increment++) {
				const char *p=basename;
				const char *end_p=strrchr(p,'.');
				if (end_p == NULL)
					end_p=strrchr(p,'\0');

				for (;;p++) {
					if (p >= end_p)
						return;

					if (*p >= '0' && *p <= '9')
						break;
					}

				filename new_basename;
				memcpy(new_basename.Name,basename,p - basename);
				new_basename.Name[p - basename]='\0';

				sprintf(new_basename.Name + strlen(new_basename.Name),
										"%u",((uint)atoi(p)) + increment);
				while (*p >= '0' && *p <= '9')
					p++;
				strcat(new_basename.Name,p);

				filename directory=image_fname;		directory.StripBaseName();

				filename new_fullname(directory,new_basename);

				if (file::Exists(new_fullname)) {
					load_image((const char *)new_fullname);
					break;
					}
				}
			}

	void load_from_imagesource(IMAGE::SOURCE * const imagesource,
								const char * const shooting_info_fname=NULL)
		{
			processor.start_operation(
				interactive_image_processor_t::LOAD_FILE,
									shooting_info_fname,imagesource);
			enable_disable_controls();
			}

	void update_crop_view_label(void)
		{
			const vec<uint> image_size=processor.get_image_size();

			vec<uint> target_size=
									{3570,2516};	// Frontier A4
									// {2752,1830};	// Frontier 15x23 !!!
									// {1818,1228};	// Frontier 10x15 !!!

			if ((image_size.y > image_size.x) !=
								(target_size.y > target_size.x))
				target_size.exchange_components();

			char buf[200];
			sprintf(buf,"%ux%u %+.2f%%",image_size.x,image_size.y,
							(image_size.x*target_size.y /
							(float)(image_size.y*target_size.x) - 1) * 100);
			crop_info_qlabel->setText(buf);
			}

	void select_normal_view(void)
		{
			normal_view_hbox->show();
			color_balance_view_hbox->hide();
			crop_view_hbox->hide();
			}

	void select_color_balance_view(void)
		{
			normal_view_hbox->hide();
			color_balance_view_hbox->show();
			crop_view_hbox->hide();
			}

	void select_crop_view(void)
		{
			normal_view_hbox->hide();
			color_balance_view_hbox->hide();
			crop_view_hbox->show();
			}

	void shooting_info_dialog(void)
		{
			const image_reader_t::shooting_info_t info=
											processor.get_shooting_info();
			char buf[500];

			*buf='\0';
			if (info.ISO_speed)
				sprintf(strchr(buf,'\0'),"ISO speed: %u\n",info.ISO_speed);
			if (info.aperture > 0)
				sprintf(strchr(buf,'\0'),"Aperture: f/%.1f\n",info.aperture);
			if (info.exposure_time > 0) {
				if (info.exposure_time <= 0.5)
					sprintf(strchr(buf,'\0'),"Exposure: 1/%.0f\n",
												1 / info.exposure_time);
				  else
					sprintf(strchr(buf,'\0'),"Exposure: %.1f sec\n",
												info.exposure_time);
				}
			if (info.focal_length_mm > 0)
				sprintf(strchr(buf,'\0'),"Focal length: %.0fmm\n",
													info.focal_length_mm);

			QMessageBox::information(this,"Shooting info",buf,
								QMessageBox::Ok,QMessageBox::NoButton);
			}

	public:

	interactive_image_processor_t processor;
	QSettings settings;

	image_window_t(QApplication * const app);
	};

void image_widget_t::ensure_correct_size(void)
{
	const vec<uint> image_size=image_window->processor.get_image_size();
	const vec<uint> window_size={width(),height()};

	vec<uint> desired_size=image_size;
	if (desired_size.x > window_size.x) {
		desired_size.x = window_size.x;
		desired_size.y=(desired_size.x*image_size.y + image_size.x/2) /
															image_size.x;
		}
	if (desired_size.y > window_size.y) {
		desired_size.y = window_size.y;
		desired_size.x=(desired_size.y*image_size.x + image_size.y/2) /
															image_size.y;
		}

	const QSize qdesired_size(desired_size.x,desired_size.y);

	if (qimage.size() == qdesired_size)
		return;

	qimage.create(qdesired_size,32);
	qimage.fill(0);

	union {
		uchar chars[4];
		QRgb rgb;
		} endian_test_union;
	memset(&endian_test_union,0,sizeof(endian_test_union));
	endian_test_union.chars[0]=1;

	image_window->processor.set_working_res(qimage.width(),qimage.height(),
							qimage.bits(),!!qBlue(endian_test_union.rgb),4);
	}

void image_widget_t::mousePressEvent(QMouseEvent *e)
{
	uint values_in_file[3];
	image_window->processor.get_spot_values(
							e->x() / (float)qpixmap.width(),
							e->y() / (float)qpixmap.height(),
							values_in_file);

	//!!! this gives wrong results now that qimage.size() != image_widget.size()

	const QRgb screen_rgb=qimage.pixel(e->x(),e->y());

	char buf[200];
	sprintf(buf,"Values in file: %u/%u/%u\n"
				"RGB on screen: %u/%u/%u",	values_in_file[0],
											values_in_file[1],
											values_in_file[2],
											qRed(screen_rgb),
											qGreen(screen_rgb),
											qBlue(screen_rgb));
	QMessageBox::information(this,MESSAGE_BOX_CAPTION,buf,
						QMessageBox::Ok,QMessageBox::NoButton);
	}

void image_widget_t::resizeEvent(QResizeEvent *)
{
	image_window->check_processing();
	}

class external_reader_process_t : public QProcess, public IMAGE::SOURCE {
	Q_OBJECT

	image_window_t * const image_window;
	filename shooting_info_fname;
	MEMBLOCK mb;
	uint cur_read_pos;

	public slots:

	void read_more_data(void)
		{
			const QByteArray array=readStdout();
			if (array.count())
				mb.AppendMem(array.data(),array.count());
			}

	void load_image(void)
		{
			read_more_data();
			image_window->load_from_imagesource(this,shooting_info_fname);
			}

	public:

	virtual void Seek(int pos) { cur_read_pos=(uint)pos; }
	virtual void SeekRel(int offs)
					{ cur_read_pos=(uint)(((sint)cur_read_pos) + offs); }

	virtual void Read(void *buf,int len)
		{
			memcpy(buf,((const char *)mb.Ptr) + cur_read_pos,len);
			cur_read_pos+=len;
			}

	virtual int ReadTillEof(void *buf,int len)
		{
			const uint len_left=((uint)mb.Len) - cur_read_pos;
			if (len > (sint)len_left)
				len = (sint)len_left;
			Read(buf,len);
			return len;
			}

	virtual ~external_reader_process_t(void) {}

	external_reader_process_t(image_window_t * const _image_window,
												const QStringList &args,
						const char * const _shooting_info_fname=NULL) :
				QProcess(args,_image_window,"external image reader process"),
				image_window(_image_window), cur_read_pos(0)
		{
			if (_shooting_info_fname != NULL)
				shooting_info_fname.Set(_shooting_info_fname);

			setCommunication(QProcess::Stdout | QProcess::Stderr);

			connect(this,SIGNAL(readyReadStdout()),SLOT(read_more_data()));
			connect(this,SIGNAL(processExited()),SLOT(load_image()));
			}

	uint launch(void) { return QProcess::launch(""); }
	};

image_window_t::image_window_t(QApplication * const app) :
				QMainWindow(NULL,"image_window"),
				image_load_filesource(NULL), external_reader_process(NULL),
				file_menu(this), processor(this)
{
	QVBox * const qvbox=new QVBox(this);
	setCentralWidget(qvbox);

	QHBox * const qhbox=new QHBox(qvbox);

		/*******************************/
		/*****                     *****/
		/***** normal view widgets *****/
		/*****                     *****/
		/*******************************/

	normal_view_hbox=new QHBox(qhbox);
	normal_view_hbox->setSpacing(10);

	contrast_slider=new slider_t(normal_view_hbox,"Contrast",0.5,4,1.6,"%.2fx");
	connect(contrast_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	exposure_slider=new slider_t(normal_view_hbox,"Exposure",-4,+4,0,"%+.2f");
	connect(exposure_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	black_level_slider=
				new slider_t(normal_view_hbox,"Black level",0,100,0,"%.0f");
	connect(black_level_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	white_clipping_slider=
				new slider_t(normal_view_hbox,"White level",0,3,0,"%.2f");
	connect(white_clipping_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

		/**************************************/
		/*****                            *****/
		/***** color balance view widgets *****/
		/*****                            *****/
		/**************************************/

	color_balance_view_hbox=new QHBox(qhbox);
	color_balance_view_hbox->setSpacing(10);

	red_balance_slider=new slider_t(color_balance_view_hbox,
											"Red coeff",0.5,1.5,1,"%.2f");
	connect(red_balance_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	blue_balance_slider=new slider_t(color_balance_view_hbox,
											"Blue coeff",0.5,1.5,1,"%.2f");
	connect(blue_balance_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	grayscale_checkbox=new QCheckBox("Grayscale",color_balance_view_hbox);
	connect(grayscale_checkbox,SIGNAL(toggled(bool)),
									SLOT(color_and_levels_params_changed()));

		/*****************************/
		/*****                   *****/
		/***** crop view widgets *****/
		/*****                   *****/
		/*****************************/

	crop_view_hbox=new QHBox(qhbox);
	crop_view_hbox->setSpacing(10);

	crop_info_qlabel=new QLabel(crop_view_hbox);

	top_crop=new crop_spin_box_t(crop_view_hbox,"Top crop");
	connect(top_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	bottom_crop=new crop_spin_box_t(crop_view_hbox,"Bottom crop");
	connect(bottom_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	left_crop=new crop_spin_box_t(crop_view_hbox,"Left crop");
	connect(left_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	right_crop=new crop_spin_box_t(crop_view_hbox,"Right crop");
	connect(right_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));

	const uint fixed_height=crop_view_hbox->sizeHint().height();
	normal_view_hbox->setFixedHeight(fixed_height);
	color_balance_view_hbox->setFixedHeight(fixed_height);
	crop_view_hbox->setFixedHeight(fixed_height);

	image_widget=new image_widget_t(qvbox,this);
	qvbox->setStretchFactor(image_widget,1000);

	menuBar()->insertItem("&File",&file_menu);

	file_menu.insertItem("&Open image..",
					this,SLOT(open_file_dialog()),CTRL + Key_O);
	file_menu.insertItem("Open &next numbered image",
					this,SLOT(open_next_numbered_image()),CTRL + Key_N);
	file_menu.insertItem("Save &As..",
					this,SLOT(save_as()),CTRL + Key_A);
	file_menu.insertItem("E&xit",
					app,SLOT(quit()),CTRL + Key_Q);
	file_menu.insertSeparator();

	set_recent_images_in_file_menu();
	connect(&file_menu,SIGNAL(activated(int)),SLOT(load_recent_image(int)));

	QPopupMenu * const view_menu=new QPopupMenu(this);
	view_menu->insertItem("&Normal",this,SLOT(select_normal_view()),Key_F5);
	view_menu->insertItem("Color &Balance",this,
								SLOT(select_color_balance_view()),Key_F6);
	view_menu->insertItem("&Crop",this,SLOT(select_crop_view()),Key_F7);
	view_menu->insertItem("Shooting &Info",this,SLOT(shooting_info_dialog()),CTRL + Key_I);

	menuBar()->insertItem("&View",view_menu);

	select_normal_view();

	resize(1280,1024);		//!!!

	processor.set_enh_shadows(0 /*!!!*/);
	color_and_levels_params_changed();
	enable_disable_controls();
	}

void image_window_t::set_recent_images_in_file_menu(void)
{
	for (uint i=1;i <= NR_OF_IMAGES_TO_REMEMBER;i++) {
		if (file_menu.indexOf((sint)i) >= 0)
			file_menu.removeItem((sint)i);

		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_images/%u",i);

		QString entry=settings.readEntry(settings_key);
		if (entry.isNull())
			continue;

		entry.prepend(' ');
		entry.prepend(QString::number(i));
		if (i < 10)
			entry.prepend('&');
		file_menu.insertItem(entry,(sint)i);
		}
	}

void image_window_t::load_image(const QString &fname_string)
{
	if (fname_string.isNull() || image_load_filesource != NULL ||
											external_reader_process != NULL)
		return;

	const filename fname(fname_string.latin1());

	if (!*fname || !file::Exists(fname)) {
		char buf[800];
		sprintf(buf,"File %s not found",(const char *)fname);
		QMessageBox::warning(this,MESSAGE_BOX_CAPTION,buf,
						QMessageBox::Ok,QMessageBox::NoButton);
		return;
		}

		// start loading image

	if (QString(fname.Extension()).lower() == "crw") {
		QStringList args;
		args << "crw";
		args << "-d";			// Dillon interpolation
		args << "-3";			// 48-bit .psd output
		args << "-c";			// output to stdout
		args << "-b" << "3.8";	// 3.8x brightness
		args << "-r" << "1.08";	//  red scaling to fix the green hue in clouds
		args << "-l" << "1.03";	// blue scaling to fix the green hue in clouds
		args << fname.Name;

		/*	formula to convert exposure and white level values from old
			"2.4x brightness 0.4 invariant density" system to current system:

			exposure_stops=3.32 * (exposure_D - 0.4 + 0.2/contrast)
			white_stops=3.32 * white_D

			*/

		external_reader_process=new external_reader_process_t(
													this,args,fname.Name);
		if (!external_reader_process->launch()) {
			delete external_reader_process;
			external_reader_process=NULL;

			QMessageBox::warning(this,MESSAGE_BOX_CAPTION,
						"crw reader process could not be started",
						QMessageBox::Ok,QMessageBox::NoButton);
			return;
			}
		}
	  else {
		image_load_filesource=new IMAGE::FILESOURCE(fname);
		load_from_imagesource(image_load_filesource);
		}

	image_fname.Set(fname);
	enable_disable_controls();

		// update recent images list

	{QString recent_images[NR_OF_IMAGES_TO_REMEMBER];
	recent_images[0]=QFileInfo(QString(fname)).absFilePath();
	uint nr_of_recent_images=1;

	uint i;
	for (i=1;i <= NR_OF_IMAGES_TO_REMEMBER &&
				nr_of_recent_images < NR_OF_IMAGES_TO_REMEMBER;i++) {
		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_images/%u",i);

		const QString entry=settings.readEntry(settings_key);
		if (entry.isNull())
			continue;

		if (entry == recent_images[0])
			continue;

		recent_images[nr_of_recent_images++]=entry;
		}

	for (i=0;i < nr_of_recent_images;i++) {
		char settings_key[500];
		sprintf(settings_key,SETTINGS_PREFIX "recent_images/%u",i + 1);

		settings.writeEntry(settings_key,recent_images[i]);
		}

	set_recent_images_in_file_menu();}
	}

void image_window_t::crop_params_changed(void)
{
	processor.set_crop(	top_crop->get_value(),
						bottom_crop->get_value(),
						left_crop->get_value(),
						right_crop->get_value());
	check_processing();
	}

void image_window_t::color_and_levels_params_changed(void)
{
	color_and_levels_processing_t::params_t params;

	params.black_level=pow(black_level_slider->get_value() / 255,2.2);
	params.white_clipping_stops=white_clipping_slider->get_value();

	params.contrast=contrast_slider->get_value();
	params.exposure_shift=exposure_slider->get_value();

	params.color_coeffs[0]=red_balance_slider->get_value();
	params.color_coeffs[1]=1;
	params.color_coeffs[2]=blue_balance_slider->get_value();
	params.convert_to_grayscale=grayscale_checkbox->isChecked();

	processor.set_color_and_levels_params(params);
	check_processing();
	}

void image_window_t::enable_disable_controls(void)
{
	const char * status=processor.operation_pending_count ?
												"processing..." : "idle";
	if (external_reader_process != NULL && !processor.operation_pending_count)
		status="running external image reader process...";

	setCaption(QString(image_fname) + ":  " + status);
	}

void image_window_t::check_processing(void)
{
	if (processor.operation_pending_count)
		return;

	if (image_load_filesource != NULL) {
		delete image_load_filesource;
		image_load_filesource=NULL;
		}

	if (external_reader_process != NULL) {
		delete external_reader_process;
		external_reader_process=NULL;
		}

	image_widget->ensure_correct_size();

	if (processor.is_processing_necessary && processor.is_file_loaded)
		processor.start_operation(interactive_image_processor_t::PROCESSING);

	enable_disable_controls();
	}

bool image_window_t::event(QEvent *e)
{
	if (e->type() != QEvent::User)
		return QMainWindow::event(e);

		// operation_completed() event

	interactive_image_processor_t::operation_type_t operation_type;
	char *error_text;
	while (processor.get_operation_results(operation_type,error_text)) {
		if (error_text != NULL) {
			//!!!
			delete [] error_text;
			continue;
			}

		update_crop_view_label();

		if (operation_type == interactive_image_processor_t::PROCESSING)
			image_widget->refresh_image();
		}

	check_processing();

	return true;
	}

void image_window_t::operation_completed(void)
{			// called in interactive_image_processor_t's thread

	QThread::postEvent(this,new QEvent(QEvent::User));
	}

int main(sint argc,char **argv)
{
	if (file::Exists("DbgPrintf.log"))
		Debug.OpenLog("DbgPrintf.log");

	QApplication app(argc,argv);

	image_window_t main_window(&app);
	app.setMainWidget(&main_window);
	main_window.show();

	if (argc > 1)
		main_window.load_image(argv[1]);

	return app.exec();
	}

#include "qt-main.moc"
