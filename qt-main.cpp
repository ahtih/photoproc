/* Copyright (C) 2003 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <qapplication.h>
#include <qpopupmenu.h>
#include <qmenubar.h>
#include <qmainwindow.h>
#include <qcolor.h>
#include <qregexp.h>
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

#include "processing.hpp"
#include "interactive-processor.hpp"

#define MESSAGE_BOX_CAPTION 		"photoproc"
#define SETTINGS_PREFIX				"/photoproc/"

#define NR_OF_IMAGES_TO_REMEMBER	20

class external_reader_process_t : public QProcess {
	Q_OBJECT

	interactive_image_processor_t * const processor;
	QObject * const notification_receiver;
	QString shooting_info_fname;

	void *buf;
	uint buf_len;
	uint buf_used_len;

	public slots:

	void read_more_data(void)
		{
			const QByteArray array=readStdout();
			if (!array.count())
				return;

			if (buf_used_len + array.count() > buf_len) {
				buf_len=buf_used_len + array.count() + (4U << 20);
				buf=realloc(buf,buf_len);
				}

			memcpy(((char *)buf) + buf_used_len,array.data(),array.count());
			buf_used_len+=array.count();
			}

	void process_finished(void)
		{
			read_more_data();

			processor->start_operation(
				interactive_image_processor_t::LOAD_FROM_MEMORY,
								shooting_info_fname.latin1(),buf,buf_used_len);
			buf=NULL;
			is_finished=1;

			if (notification_receiver != NULL)
				QApplication::postEvent(notification_receiver,
													new QEvent(QEvent::User));
			}

	public:

	uint is_finished;

	virtual ~external_reader_process_t(void)
		{
			if (buf != NULL) {
				free(buf);
				buf=NULL;
				}
			}

	external_reader_process_t(interactive_image_processor_t * const _processor,
				QObject * const _notification_receiver,const QStringList &args,
					const QString &_shooting_info_fname=(const char *)NULL) :
				QProcess(args,NULL,"external image reader process"),
				processor(_processor),
				notification_receiver(_notification_receiver),
				buf(NULL), buf_len(0), buf_used_len(0), is_finished(0)
		{
			if (_shooting_info_fname != NULL)
				shooting_info_fname=_shooting_info_fname;

			setCommunication(QProcess::Stdout | QProcess::Stderr);

			connect(this,SIGNAL(readyReadStdout()),SLOT(read_more_data()));
			connect(this,SIGNAL(processExited()),SLOT(process_finished()));
			}

	uint launch(void) { return QProcess::launch(""); }
	};

class processor_t :
			private interactive_image_processor_t::notification_receiver_t {
	QObject * const notification_receiver;
	external_reader_process_t *external_reader_process;
							// NULL if no external_reader_process in progress

	virtual void operation_completed(void)
		{			// called in interactive_image_processor_t's thread
			if (notification_receiver != NULL)
				QApplication::postEvent(notification_receiver,
													new QEvent(QEvent::User));
			}

	public:

	interactive_image_processor_t processor;

	uint is_external_reader_process_running(void) const
		{
			if (external_reader_process == NULL)
				return 0;
			return !external_reader_process->is_finished;
			}

	void delete_external_reader_process(void)
		{
			if (external_reader_process != NULL) {
				delete external_reader_process;
				external_reader_process=NULL;
				}
			}

	processor_t(QObject * const _notification_receiver=NULL) :
							notification_receiver(_notification_receiver),
							external_reader_process(NULL), processor(this) {}
	virtual ~processor_t(void) { delete_external_reader_process(); }

	QString start_loading_image(const QString &fname);
		// returns error text, or null string if no error

	QString get_shooting_info_text(void)
		{
			const image_reader_t::shooting_info_t info=
											processor.get_shooting_info();
			char buf[500];

			*buf='\0';
			if (*info.camera_type)
				sprintf(strchr(buf,'\0'),"Camera: %s\n",info.camera_type);
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
			return buf;
			}
	};

QString processor_t::start_loading_image(const QString &fname)
{		// returns error text, or null string if no error

	const QFileInfo fileinfo(fname);

	if (fname.isEmpty() || !fileinfo.exists()) {
		char buf[800];
		sprintf(buf,"File %s not found",fname.latin1());
		return buf;
		}

		// start loading image

	if (fileinfo.extension(FALSE).lower() == "crw") {
		QStringList args;
		args << "dcraw";
		args << "-3";			// 48-bit .psd output
		args << "-c";			// output to stdout
		args << "-b" << "3.8";	// 3.8x brightness
		args << "-r" << "1.08";	//  red scaling to fix green hue in clouds
		args << "-l" << "1.03";	// blue scaling to fix green hue in clouds
		args << fname;

		/*	formula to convert exposure and white level values from old
			"2.4x brightness 0.4 invariant density" system to current system:

			exposure_stops=3.32 * (exposure_D - 0.4 + 0.2/contrast)
			white_stops=3.32 * white_D

			*/

		external_reader_process=new external_reader_process_t(
								&processor,notification_receiver,args,fname);

		if (!external_reader_process->launch()) {
			delete external_reader_process;
			external_reader_process=NULL;

			return "external reader process could not be started";
			}
		}
	  else
		processor.start_operation(interactive_image_processor_t::LOAD_FILE,
															fname.latin1());
	return QString();
	}

class image_window_t;

class image_widget_t : public QWidget {
    Q_OBJECT
	image_window_t * const image_window;
	QImage qimage;
	QPixmap qpixmap;

	vec<uint> get_image_offset(void) const
		{
			const vec<uint> offset={( width()-qpixmap. width()) / 2,
									(height()-qpixmap.height()) / 2};
			return offset;
			}

	void do_bitblt(void)
		{
			const vec<uint> offset=get_image_offset();

			erase(QRegion(rect(),QRegion::Rectangle).subtract(
					QRegion(offset.x,offset.y,qpixmap.width(),qpixmap.height(),
											QRegion::Rectangle)));
			bitBlt(this,offset.x,offset.y,&qpixmap);
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
			qpixmap.convertFromImage(qimage);
			do_bitblt();
			}
	};

class slider_t : public QHBox {
	Q_OBJECT

	const QString display_format;
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
			spinbox=new QSpinBox(0,9999,20,this);
			}

	uint get_value(void) const { return (uint)spinbox->value(); }
	};

class image_window_t : public QMainWindow, public processor_t {
	Q_OBJECT
	protected:

	QString image_fname;

	QPopupMenu file_menu;
	image_widget_t *image_widget;

	QHBox *normal_view_hbox;
	slider_t *contrast_slider,*exposure_slider;
	slider_t *black_level_slider,*white_clipping_slider;

	QHBox *color_balance_view_hbox;
	slider_t *red_balance_slider,*blue_balance_slider;
	QCheckBox *grayscale_checkbox;

	QHBox *crop_view_hbox;
	QComboBox *crop_target_combobox;
	QLabel *crop_info_qlabel;
	crop_spin_box_t *top_crop,*bottom_crop,*left_crop,*right_crop;

	void set_recent_images_in_file_menu(void);
	void save_window_pos(void);

	static const struct output_dimensions_t {
		const char *name;
		vec<uint> dimensions;
		} output_dimensions[];

	virtual bool event(QEvent *e);
	virtual void moveEvent(QMoveEvent *e)
		{
			QMainWindow::moveEvent(e);
			save_window_pos();
			}

	virtual void resizeEvent(QResizeEvent *e)
		{
			QMainWindow::resizeEvent(e);
			save_window_pos();
			}

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
			if (image_fname.isEmpty())
				return;

			QString save_fname=image_fname;
			save_fname.replace(QRegExp("^.*[\\\\/]"),"");
			save_fname.replace(QRegExp("\\.[^.]*$"),"");
			save_fname+=".bmp";

			const QString fname=QFileDialog::getSaveFileName(save_fname,
						"BMP files (*.bmp)",this,"save as dialog","Save As");
			if (fname.isEmpty())
				return;

			processor.start_operation(interactive_image_processor_t::
										FULLRES_PROCESSING,fname.latin1());
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
			if (image_fname.isEmpty())
				return;

			const sint number_idx=image_fname.find(QRegExp("[0-9][^\\\\/]*$"));

			if (number_idx < 0)
				return;

			QString number_str=image_fname.mid(number_idx);
			number_str.replace(QRegExp("[^0-9].*$"),"");
			const uint number=number_str.toUInt();

			QString suffix;
			const sint suffix_idx=image_fname.findRev(
												QRegExp("\\.[^\\\\/]*$"));
			if (suffix_idx >= 0)
				suffix=image_fname.mid(suffix_idx);

			for (uint increment=1;increment < 100;increment++) {
				QString new_number_str=QString::number(number + increment);
				while (new_number_str.length() < number_str.length())
					new_number_str="0" + new_number_str;

				const QString new_fname=image_fname.left(number_idx) +
												new_number_str + suffix;

				if (QFileInfo(new_fname).exists()) {
					load_image(new_fname);
					break;
					}
				}
			}

	void update_crop_view_label(void)
		{
			if (image_fname.isEmpty()) {
				crop_info_qlabel->setText("");
				return;
				}

			const vec<uint> image_size=processor.get_image_size();

			vec<uint> target_size=output_dimensions[
							crop_target_combobox->currentItem()].dimensions;

			if ((image_size.y > image_size.x) !=
								(target_size.y > target_size.x))
				target_size.exchange_components();

			char buf[200];
			sprintf(buf,"%+.2f%% %ux%u",
							(image_size.x*target_size.y /
							(float)(image_size.y*target_size.x) - 1) * 100,
												image_size.x,image_size.y);
			crop_info_qlabel->setText(buf);
			}

	void target_dimensions_changed(void);

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
			QMessageBox::information(this,"Shooting info",
								get_shooting_info_text(),
								QMessageBox::Ok,QMessageBox::NoButton);
			}

	public:

	QSettings settings;

	image_window_t(QApplication * const app);
	void load_window_pos(void);
	};

const image_window_t::output_dimensions_t image_window_t::output_dimensions[]={
		{"A4 Frontier",		{3570,2516}},
		{"15x23 Frontier",	{2752,1830}},
		{"10x15 Frontier",	{1818,1228}},
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
	const vec<uint> image_offset=get_image_offset();
	const vec<sint> pos={	e->x() - (sint)image_offset.x,
							e->y() - (sint)image_offset.y};

	const vec<double> pos_fraction={pos.x / (double)qpixmap.width(),
									pos.y / (double)qpixmap.height()};

	if (pos.x < 0 || pos.y < 0 || pos_fraction.x >= 1 || pos_fraction.y >= 1)
		return;

	uint values_in_file[3];
	image_window->processor.get_spot_values(pos_fraction.x,pos_fraction.y,
															values_in_file);

	const QRgb screen_rgb=qimage.pixel(pos.x,pos.y);

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

image_window_t::image_window_t(QApplication * const app) :
		QMainWindow(NULL,"image_window"), processor_t(this), file_menu(this)
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

	crop_target_combobox=new QComboBox((bool)0,crop_view_hbox);
	crop_target_combobox->setMinimumWidth(100);

	{ const QString selected_target_dimensions=settings.readEntry(
								SETTINGS_PREFIX "selected_target_dimensions");

	for (uint i=0;i < lenof(output_dimensions);i++) {
		const QString name=QString(output_dimensions[i].name);
		crop_target_combobox->insertItem(name +
			" (" + QString::number(output_dimensions[i].dimensions.x) +
			"x"  + QString::number(output_dimensions[i].dimensions.y) + ")");
		if (name == selected_target_dimensions)
			crop_target_combobox->setCurrentItem(i);
		}}

	connect(crop_target_combobox,SIGNAL(activated(int)),
									SLOT(target_dimensions_changed(void)));

	crop_info_qlabel=new QLabel(crop_view_hbox);

	top_crop=new crop_spin_box_t(crop_view_hbox,"Crop: Top");
	connect(top_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	bottom_crop=new crop_spin_box_t(crop_view_hbox,"Bottom");
	connect(bottom_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	left_crop=new crop_spin_box_t(crop_view_hbox,"Left");
	connect(left_crop->spinbox,SIGNAL(valueChanged(int)),
												SLOT(crop_params_changed()));
	right_crop=new crop_spin_box_t(crop_view_hbox,"Right");
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

	processor.set_enh_shadows(0 /*!!!*/);
	color_and_levels_params_changed();
	enable_disable_controls();
	}

void image_window_t::target_dimensions_changed(void)
{
	settings.writeEntry(SETTINGS_PREFIX "selected_target_dimensions",
				output_dimensions[crop_target_combobox->currentItem()].name);
	update_crop_view_label();
	}

void image_window_t::save_window_pos(void)
{
	if (!isVisible() || !isActiveWindow())
		return;

	settings.writeEntry(SETTINGS_PREFIX  "pos/x",pos().x());
	settings.writeEntry(SETTINGS_PREFIX  "pos/y",pos().y());
	settings.writeEntry(SETTINGS_PREFIX "size/x",size().width());
	settings.writeEntry(SETTINGS_PREFIX "size/y",size().height());
	}

void image_window_t::load_window_pos(void)
{
	bool pos_x_ok=(bool)0;
	const sint pos_x=settings.readNumEntry(SETTINGS_PREFIX "pos/x",0,&pos_x_ok);
	bool pos_y_ok=(bool)0;
	const sint pos_y=settings.readNumEntry(SETTINGS_PREFIX "pos/y",0,&pos_y_ok);
	if (pos_x_ok && pos_y_ok)
		move(QPoint(pos_x,pos_y));

	const sint size_x=settings.readNumEntry(SETTINGS_PREFIX "size/x");
	const sint size_y=settings.readNumEntry(SETTINGS_PREFIX "size/y");
	if (size_x > 0 && size_y > 0)
		resize(QSize(size_x,size_y));
	  else
		resize(640,480);
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

void image_window_t::load_image(const QString &fname)
{
	if (processor.operation_pending_count ||
									is_external_reader_process_running())
		return;

	const QString error_text=start_loading_image(fname);
	if (!error_text.isNull()) {
		QMessageBox::warning(this,MESSAGE_BOX_CAPTION,error_text,
								QMessageBox::Ok,QMessageBox::NoButton);
		return;
		}

	image_fname=fname;
	enable_disable_controls();

		// update recent images list

	{ const QFileInfo fileinfo(fname);

	QString recent_images[NR_OF_IMAGES_TO_REMEMBER];
	recent_images[0]=fileinfo.absFilePath();
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
	if (!processor.operation_pending_count &&
										is_external_reader_process_running())
		status="running external image reader process...";

	setCaption(image_fname + ":  " + status);
	}

void image_window_t::check_processing(void)
{
	if (processor.operation_pending_count)
		return;

	if (!is_external_reader_process_running())
		delete_external_reader_process();

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

	enable_disable_controls();

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

class print_file_info_t : public QObject, public processor_t {
    Q_OBJECT
	QStringList fnames;

	void load_next(void)
		{
			if (fnames.isEmpty()) {
				QApplication::exit();
				return;
				}

			const QString error_text=start_loading_image(fnames.first());
			if (!error_text.isNull()) {
				fprintf(stderr,"%s\n",error_text.latin1());
				QApplication::exit(EXIT_FAILURE);
				}
			}

	virtual bool event(QEvent *e)
		{
			if (e->type() != QEvent::User)
				return QObject::event(e);

			interactive_image_processor_t::operation_type_t operation_type;
			char *error_text;
			while (processor.get_operation_results(operation_type,error_text))
				;

			//!!! handle errors

			if (!processor.operation_pending_count &&
									!is_external_reader_process_running()) {
				printf("File: %s\n%s",fnames.first().latin1(),
										get_shooting_info_text().latin1());
				fnames.remove(fnames.begin());

				if (!fnames.isEmpty())
					printf("\n");

				load_next();
				}

			return true;
			}

	public:
	print_file_info_t(const QStringList &_fnames) :
									processor_t(this), fnames(_fnames)
		{
			load_next();
			startTimer(300);	// without it, event cycle somehow does not
								//   work through ssh
			}
	};

int main(sint argc,char **argv)
{
	QApplication app(argc,argv);

	uint show_only_info=0;
	QStringList fnames;
	for (uint i=1;i < (uint)app.argc();i++)
		if (app.argv()[i] == QString("-info"))
			show_only_info=1;
		  else
			fnames.append(app.argv()[i]);

	if (show_only_info && !fnames.isEmpty()) {
		print_file_info_t print_file_info(fnames);
		return app.exec();
		}

	image_window_t main_window(&app);
	app.setMainWidget(&main_window);
	main_window.load_window_pos();
	main_window.show();

	if (!fnames.isEmpty())
		main_window.load_image(fnames.first());

	return app.exec();
	}

#include "qt-main.moc"
