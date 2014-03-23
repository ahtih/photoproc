/* Copyright (C) 2003-2005 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <qapplication.h>
#include <qclipboard.h>
#include <q3popupmenu.h>
#include <qmenubar.h>
#include <q3mainwindow.h>
#include <qcolor.h>
#include <qregexp.h>
#include <qpixmap.h>
#include <qlayout.h>
#include <q3gridlayout.h>
#include <qlabel.h>
#include <qslider.h>
#include <q3hbox.h>
#include <q3vbox.h>
#include <qpainter.h>
#include <qpushbutton.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qcheckbox.h>
#include <qimage.h>
#include <qthread.h>
#include <q3process.h>
#include <q3filedialog.h>
#include <qmessagebox.h>
#include <qsettings.h>
#include <qevent.h>

#include <unistd.h>

#include "processing.hpp"
#include "interactive-processor.hpp"
#include "color-patches-detector.hpp"

#define PHOTOPROC_VERSION			"0.96"

#define MESSAGE_BOX_CAPTION 		"photoproc"
#define SETTINGS_PREFIX				"/photoproc/"

#define NR_OF_IMAGES_TO_REMEMBER	20

#if QT_VERSION < 0x030100
#define POST_EVENT QThread::postEvent
#else
#define POST_EVENT QApplication::postEvent
#endif

class external_reader_process_t : public Q3Process {
	Q_OBJECT

	interactive_image_processor_t * const processor;
	QObject * const notification_receiver;
	const interactive_image_processor_t::operation_type_t operation_type;
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

			processor->start_operation(operation_type,
								shooting_info_fname.latin1(),buf,buf_used_len);
			buf=NULL;
			is_finished=1;

			if (notification_receiver != NULL)
				POST_EVENT(notification_receiver,new QEvent(QEvent::User));
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
				interactive_image_processor_t::operation_type_t
															_operation_type=
							interactive_image_processor_t::LOAD_FROM_MEMORY,
				const QString &_shooting_info_fname=(const char *)NULL) :

				Q3Process(args,NULL,"external image reader process"),
				processor(_processor),
				notification_receiver(_notification_receiver),
				operation_type(_operation_type),
				buf(NULL), buf_len(0), buf_used_len(0), is_finished(0)
		{
			if (_shooting_info_fname != NULL)
				shooting_info_fname=_shooting_info_fname;

			setCommunication(Q3Process::Stdout | Q3Process::Stderr);

			connect(this,SIGNAL(readyReadStdout()),SLOT(read_more_data()));
			connect(this,SIGNAL(processExited()),SLOT(process_finished()));
			}

	uint launch(void) { return Q3Process::launch(QString("")); }
	};

class processor_t :
			private interactive_image_processor_t::notification_receiver_t {
	QObject * const notification_receiver;
	external_reader_process_t *external_reader_process;
							// NULL if no external_reader_process in progress

	virtual void operation_completed(void)
		{			// called in interactive_image_processor_t's thread
			if (notification_receiver != NULL)
				POST_EVENT(notification_receiver,new QEvent(QEvent::User));
			}

	public:

	interactive_image_processor_t processor;

#ifndef PHOTOPROC_ALWAYS_USE_HALFRES
	QByteArray image_file_data;		// only nonempty if the currently loaded
									//   image was loaded with half-res;
									//   in this case we need image_file_data
									//   for later full-res loading
#endif

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

	QString start_loading_image(const QString &fname,const uint load_fullres,
									const QString &temp_fname=QString::null);
		// returns error text, or null string if no error

	QString get_shooting_info_text(void)
		{
			const image_reader_t::shooting_info_t info=
											processor.get_shooting_info();
			char buf[1000];

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

			if (info.focused_distance_m_min > 0 ||
											info.focused_distance_m_max > 0) {
				sprintf(strchr(buf,'\0'),"Focused to: ");

				char min_dist[100],max_dist[100];
				sprintf(min_dist,"%.1fm",max(0,info.focused_distance_m_min));

				if (info.focused_distance_m_max > 0)
					sprintf(max_dist,"%.1fm",info.focused_distance_m_max);
				  else
					sprintf(max_dist,"inf");

				if (!strcmp(min_dist,max_dist))
					sprintf(strchr(buf,'\0'),"%s\n",min_dist);
				  else
					sprintf(strchr(buf,'\0'),"%s..%s\n",min_dist,max_dist);
				}

			if (*info.timestamp)
				sprintf(strchr(buf,'\0'),"Timestamp: %s\n",info.timestamp);

			return buf;
			}
	};

QString processor_t::start_loading_image(const QString &fname,
						const uint load_fullres,const QString &temp_fname)
{		// returns error text, or null string if no error

	const QString actual_fname_to_load=temp_fname.isEmpty() ?
														fname : temp_fname;
	const QFileInfo fileinfo(actual_fname_to_load);

	if (actual_fname_to_load.isEmpty() || !fileinfo.exists()) {
		QString str;
		return str.sprintf("File %s not found",
										actual_fname_to_load.utf8().data());
		}

#ifndef PHOTOPROC_ALWAYS_USE_HALFRES
	image_file_data.resize(0);
#endif

		// start loading image

	const QString ext=QFileInfo(fname).extension(FALSE).lower();
	if (ext == "nef" || ext == "crw" || ext == "cr2" || ext == "x-canon-raw" ||
						ext == "mrw" || ext == "orf" || ext == "dcr") {
		QStringList args;
		args << "dcraw";
		args << "-4";			// 48-bit .PPM output
		args << "-c";			// output to stdout
		// args << "-b" << "3.8";	// 3.8x brightness
		args << "-M";			// don't use embedded color matrix
		args << "-o" << "0";	// don't convert camera RGB to sRGB

#ifndef PHOTOPROC_ALWAYS_USE_HALFRES
		if (!load_fullres) {
			args << "-h";			// half-res image for fast processing

			QFile f(actual_fname_to_load);
			if (!f.open(QIODevice::Unbuffered|QIODevice::ReadOnly)) {
				QString str;
				return str.sprintf("Error opening file %s",
										actual_fname_to_load.utf8().data());
				}
			image_file_data=f.readAll();
			}
#else
		args << "-h";			// half-res image for fast processing
#endif

		args << actual_fname_to_load;

		/*	formula to convert exposure and white level values from old
			"2.4x brightness 0.4 invariant density" system to current system:

			exposure_stops=3.32 * (exposure_D - 0.4 + 0.2/contrast)
			white_stops=3.32 * white_D

			*/

		external_reader_process=new external_reader_process_t(
						&processor,notification_receiver,args,
						temp_fname.isEmpty() ?
							interactive_image_processor_t::LOAD_FROM_MEMORY :
							interactive_image_processor_t::
											LOAD_FROM_MEMORY_AND_DELETE_FILE,
						actual_fname_to_load);

		if (!external_reader_process->launch()) {
			delete external_reader_process;
			external_reader_process=NULL;

			return "Helper process (dcraw) could not be started.\n\n"
				"dcraw is a program by Dave Coffin that reads digital camera \n"
				"RAW files. To use these files in photoproc, you should \n"
				"download it from one of the following websites:\n\n"
				"http://www.cybercom.net/~dcoffin/dcraw/\n"
				"http://home.arcor.de/benjamin_lebsanft/\n"
				"http://www.insflug.org/raw/";
			}
		}
	  else
		processor.start_operation(interactive_image_processor_t::LOAD_FILE,
											actual_fname_to_load.latin1());

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

	static void draw_test_table(QPaintDevice * const pd)
		{
			QPainter qp;
			qp.begin(pd);

			const uint W=(uint)qp.window().size().width();
			const uint H=(uint)qp.window().size().height();

			const uint N=8;

			const uint M=2*(N-1);
			const uint nr_of_white_columns=N-1-3;

			const uint patch_x_size=W*39/(M*40) - 1;
			const uint patch_y_size=H*39/(M*40) - 1;

			for (uint x=0;x < N-1;x++) {
				for (uint y=0;y < N-1;y++) {
					qp.fillRect((x+N-1) * W/M,
								(y    ) * H/M,
							patch_x_size,patch_y_size,
							QColor(	0,
									(x+1) * 0xff / (N-1),
									(y+1) * 0xff / (N-1)));

					qp.fillRect((x    ) * W/M,
								(y+N-1) * H/M,
							patch_x_size,patch_y_size,
							QColor(	(N-1-y) * 0xff / (N-1),
									(N-1-x) * 0xff / (N-1),
									0));

					qp.fillRect((x+N-1) * W/M,
								(y+N-1) * H/M,
							patch_x_size,patch_y_size,
							QColor(	(N-1-y) * 0xff / (N-1),
									0,
									(x+1) * 0xff / (N-1)));
					}

				qp.fillRect( (nr_of_white_columns-2) * W/M,
						x * H/M,patch_x_size,patch_y_size,
						QColor(	(x+1) * 0xff / (N-1),0,0));

				qp.fillRect((nr_of_white_columns-1) * W/M,
						x * H/M,patch_x_size,patch_y_size,
						QColor(	0,(x+1) * 0xff / (N-1),0));

				qp.fillRect((nr_of_white_columns+2) * W/M,
						x * H/M,patch_x_size,patch_y_size,
						QColor( 0,0,(x+1) * 0xff / (N-1)));

				for (uint i=0;i < nr_of_white_columns;i++) {
					const uint c=(x+(nr_of_white_columns-1-i)*(N-1)) *
									0xff / (nr_of_white_columns*(N-1)-1);
					uint xcoord=i;
					if (i >= nr_of_white_columns-2)
						xcoord+=2;
					qp.fillRect(xcoord * W/M,x * H/M,
							patch_x_size,patch_y_size,QColor(c,c,c));
					}
				}
			}

	void do_bitblt(void)
		{
			const vec<uint> offset=get_image_offset();

			erase(QRegion(rect(),QRegion::Rectangle).subtract(
					QRegion(offset.x,offset.y,qpixmap.width(),qpixmap.height(),
											QRegion::Rectangle)));
			bitBlt(this,offset.x,offset.y,&qpixmap);

			if (qpixmap.width() <= 1)
				draw_test_table(this);
			}

	protected:

	virtual void paintEvent(QPaintEvent *) { do_bitblt(); }

	virtual void mousePressEvent(QMouseEvent *e);
	virtual void resizeEvent(QResizeEvent *);

	public:

	image_widget_t(QWidget * const parent,
									image_window_t * const _image_window) :
			QWidget(parent), image_window(_image_window), qimage(1,1,32)
			{ setEraseColor(Qt::black); }

	void ensure_correct_size(void);

	void refresh_image(void)
		{
			qpixmap.convertFromImage(qimage);
			update();
			}
	};

class slider_t : public Q3HBox {
	Q_OBJECT

	const QString display_format;
	QLabel *value_display;

	public:

	float get_value(void) const { return slider->value() / 100.0; }

	protected:

	virtual QString get_value_text(const float value)
		{
			QString str;
			return str.sprintf(display_format,value);
			}

	public slots:

	void value_changed(void)
		{
			value_display->setText(get_value_text(get_value()));
			}

	public:

	QSlider *slider;

	slider_t(QWidget * const parent,const char * const name,
					const float min_value,const float max_value,
					const float default_value,
					const char * const _display_format="%.2f") :
							Q3HBox(parent), display_format(_display_format)
		{
			setSpacing(5);
			new QLabel(QString(name) + ":",this);

			value_display=new QLabel("",this);

			slider=new QSlider(	(sint)floor(min_value*100 + 0.5),
								(sint)floor(max_value*100 + 0.5),10,
								(sint)floor(default_value*100 + 0.5),
													Qt::Horizontal,this);
			slider->setFocusPolicy(static_cast<Qt::FocusPolicy>(
									Qt::TabFocus|Qt::ClickFocus));

			QFont font;
			QFontMetrics font_metrics(font);
			value_display->setFont(font);
			value_display->setFixedWidth(font_metrics.width(
								get_value_text(slider->maxValue() / 100.0)));

			connect(slider,SIGNAL(valueChanged(int)),SLOT(value_changed()));

			value_changed();
			}
	};

class two_color_balance_slider_t : public slider_t {
	public:

	two_color_balance_slider_t(QWidget * const parent,const char * const name,
					const float min_value,const float max_value,
					const float default_value) :
					slider_t(parent,name,min_value,max_value,default_value) {}

	static float get_value2(const float value)
		{ return 2-value; }

	virtual QString get_value_text(const float value)
		{
			QString str;
			return str.sprintf("%.2f/%.2f",value,get_value2(value));
			}
	};

class crop_spin_box_t : public Q3HBox {
	Q_OBJECT
	public:

	QSpinBox *spinbox;

	crop_spin_box_t(QWidget * const parent,const char * const name) :
															Q3HBox(parent)
		{
			setSpacing(5);
			new QLabel(QString(name) + ":",this);
			spinbox=new QSpinBox(0,9999,20,this);
			}

	uint get_value(void) const { return (uint)spinbox->value(); }
	};

class persistent_checkbox_t : public QCheckBox {
	Q_OBJECT

	QSettings * const settings;
	QString settings_key;

	public slots:

	void save_state(void)
		{ settings->writeEntry(settings_key,(sint)isChecked()); }

	public:
	persistent_checkbox_t(const char * const label,QWidget * const parent,
			QSettings * const _settings,const char * const _settings_key) :
						QCheckBox(label,parent),
						settings(_settings), settings_key(_settings_key)
		{
			setChecked(settings->readNumEntry(settings_key,0));
			connect(this,SIGNAL(toggled(bool)),SLOT(save_state(void)));
			}
	};

class persistent_spinbox_t : public QSpinBox {
	Q_OBJECT

	QSettings * const settings;
	QString settings_key;

	public slots:

	void save_state(void)
		{ settings->writeEntry(settings_key,value()); }

	public:
	persistent_spinbox_t(const sint min_value,const sint max_value,
			const sint default_value,QWidget * const parent,
			QSettings * const _settings,const char * const _settings_key) :
						QSpinBox(min_value,max_value,1,parent),
						settings(_settings), settings_key(_settings_key)
		{
			setValue(settings->readNumEntry(settings_key,default_value));
			connect(this,SIGNAL(valueChanged(int)),SLOT(save_state(void)));
			}
	};

class file_save_options_dialog_t : public QDialog {
	Q_OBJECT

	image_window_t * const image_window;
	const QString fname;
	persistent_checkbox_t *resize_checkbox;
	persistent_checkbox_t *unsharp_mask_checkbox;
	persistent_spinbox_t *unsharp_mask_radius_spinbox;

	protected slots:

	void accept(void);

	public:

	file_save_options_dialog_t(image_window_t * const _image_window,
							const QString _fname,const vec<uint> &resize_size);
	};

class image_window_t : public Q3MainWindow, public processor_t {
	Q_OBJECT
	protected:

	QString image_fname;
	QString spot_values_clipboard;
	QString start_fullres_processing_fname;	// empty if fullres processing
											//    request is not pending
	uint fullres_processing_do_resize;		// 0 or 1
	float fullres_processing_USM_radius;	// <=0 if no unsharp mask

	Q3ValueList<sint> file_menu_load_save_ids;

	Q3PopupMenu file_menu;
	image_widget_t *image_widget;

	Q3HBox *normal_view_hbox;
	slider_t *contrast_slider,*exposure_slider;
	slider_t *black_level_slider,*white_clipping_slider;

	Q3HBox *color_balance_view_hbox;
	two_color_balance_slider_t *red_blue_balance_slider;
	slider_t *green_balance_slider;
	QCheckBox *grayscale_checkbox;

	Q3HBox *crop_view_hbox;
	QComboBox *crop_target_combobox;
	QLabel *crop_info_qlabel;
	crop_spin_box_t *top_crop,*bottom_crop,*left_crop,*right_crop;

	void set_recent_images_in_file_menu(void);

	static const struct output_dimensions_t {
		const char *name;
		vec<uint> dimensions;

		QString display_string(void) const
			{
				const QString dim=	QString::number(dimensions.x) +
							"x" +	QString::number(dimensions.y);

				return (name != NULL) ? QString(name) + "(" + dim + ")" : dim;
				}
				
		QString settings_id(void) const
			{
				return (name != NULL) ? QString(name) : display_string();
				}
				
		} output_dimensions[];

	void key_event(QKeyEvent * const e)
		{
			const uint shift_before=!!(e->state() & Qt::ShiftModifier);
			const uint shift_after=!!(e->stateAfter() & Qt::ShiftModifier);

			if (shift_before && !shift_after)
				QApplication::clipboard()->setText(
								spot_values_clipboard,QClipboard::Clipboard);

			if (shift_before != shift_after)
				spot_values_clipboard=QString::null;

			e->ignore();
			}

	virtual void keyPressEvent(QKeyEvent *e)
		{
			if (e->text() == "+") {
				exposure_slider->slider->addStep();
				e->ignore();
				return;
				}

			if (e->text() == "-") {
				exposure_slider->slider->subtractStep();
				e->ignore();
				return;
				}

			key_event(e);
			}

	virtual void keyReleaseEvent(QKeyEvent *e)	{ key_event(e); }

	virtual bool event(QEvent *e);
	virtual void moveEvent(QMoveEvent *e)
		{
			Q3MainWindow::moveEvent(e);
			if (isVisible() && isActiveWindow()) {
				settings.writeEntry(SETTINGS_PREFIX  "pos/x",pos().x());
				settings.writeEntry(SETTINGS_PREFIX  "pos/y",pos().y());
				}
			}

	virtual void resizeEvent(QResizeEvent *e)
		{
			Q3MainWindow::resizeEvent(e);
			if (isVisible() && isActiveWindow()) {
				settings.writeEntry(SETTINGS_PREFIX "size/x",size().width());
				settings.writeEntry(SETTINGS_PREFIX "size/y",size().height());

				if (pos().x() == -1 && pos().y() == -1) {
					const QPoint point=mapToGlobal(
						frameGeometry().topLeft() - geometry().topLeft());
					settings.writeEntry(SETTINGS_PREFIX  "pos/x",point.x());
					settings.writeEntry(SETTINGS_PREFIX  "pos/y",point.y());
					}
				}
			}

	public slots:

	void load_image(const QString &fname);
	void set_caption(void);
	void check_processing(void);
	void color_and_levels_params_changed(void);
	void crop_params_changed(void);

	void open_file_dialog(void)
		{
			QString fname=
				settings.readEntry(SETTINGS_PREFIX "recent_images/1");
			while (1) {
				fname=Q3FileDialog::getOpenFileName(fname,
					"image files (*.bmp *.tif *.tiff *.psd *.crw *.CRW "
									"*.cr2 *.CR2 *.NEF *.MRW *.ORF *.DCR)",
					this,"open image dialog","Open image");
				if (fname.isNull())
					return;
				if (!QFileInfo(fname).isDir())
					break;
				}

			load_image(fname);
			}

	void add_to_spot_values_clipboard(const QString &str)
		{
			if (!spot_values_clipboard.isEmpty())
				spot_values_clipboard+='\n';
			spot_values_clipboard+=str;
			}

	void save_as(void)
		{
			if (image_fname.isEmpty())
				return;

			QString last_save_directory=settings.readEntry(
									SETTINGS_PREFIX "last_save_directory");
			QString fname;
			while (1) {
				QString save_fname=image_fname;
				save_fname.replace(QRegExp("^.*[\\\\/]"),"");
				save_fname.replace(QRegExp("\\.[^.]*$"),"");
				save_fname+=".bmp";

				if (!last_save_directory.isEmpty())
					save_fname.prepend(last_save_directory + "/");

				fname=Q3FileDialog::getSaveFileName(save_fname,
								"BMP files (*.bmp);;JPG files (*.jpg)",
								this,"save as dialog","Save As");

				if (!QFileInfo(fname).isDir())
					break;

				last_save_directory=fname;				
				}

			if (fname.isEmpty())
				return;

			last_save_directory=fname;
			last_save_directory.replace(QRegExp("[\\\\/][^\\\\/]*$"),"");
			settings.writeEntry(SETTINGS_PREFIX "last_save_directory",
													last_save_directory);

			if (!fname.contains('.'))
				fname+=".bmp";

			(new file_save_options_dialog_t(this,fname,output_dimensions[
					crop_target_combobox->currentItem()].dimensions))->exec();
			}

	void load_recent_image(int menuitem_id)
		{
			if (menuitem_id >= 1 && menuitem_id <= NR_OF_IMAGES_TO_REMEMBER) {
				QString menuitem_text=file_menu.text(menuitem_id);
				while (menuitem_text.length() && menuitem_text.at(0) != ' ')
					menuitem_text.remove(0,1);

				load_image(menuitem_text.stripWhiteSpace());
				}
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

			for (uint increment=1;increment < 500;increment++) {
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

	void delete_file(void)
		{
		if (QMessageBox::information(this,MESSAGE_BOX_CAPTION,
				"Delete current image file?",
				QMessageBox::Yes,QMessageBox::No |
							QMessageBox::Default | QMessageBox::Escape) ==
													QMessageBox::Yes)
			QFile::remove(image_fname);
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

	void display_help_about(void)
		{
			QMessageBox::information(this,"About",
						"photoproc " PHOTOPROC_VERSION "\n\n"
						"Written by Ahti Heinla (ahti@ahti.bluemoon.ee)",
								QMessageBox::Ok,QMessageBox::NoButton);
			}

	void ensure_fullres_loaded_image(void);

	void start_fullres_processing(const QString fname,const uint do_resize,
										const float unsharp_mask_radius)
		{
			ensure_fullres_loaded_image();

			start_fullres_processing_fname=fname;
			fullres_processing_do_resize=do_resize;
			fullres_processing_USM_radius=unsharp_mask_radius;

			if (is_external_reader_process_running())
				return;		// if the process is running, then load operation
							//   might not yet have started in processor_t,
							//   and therefore we need to wait so that we
							//   don't start our operation before the image
							//   load operation

			start_fullres_processing_fname=QString::null;

			vec<uint> resize_size={0,0};
			if (do_resize)
				resize_size=output_dimensions[
							crop_target_combobox->currentItem()].dimensions;

			processor.set_fullres_processing_params(resize_size,
														unsharp_mask_radius);
			processor.start_operation(interactive_image_processor_t::
										FULLRES_PROCESSING,fname.latin1());
			set_caption();
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
		{NULL,				{1280,1024}},
		{NULL,				{1024,768}},
		{NULL,				{800,600}},
		{NULL,				{640,480}},
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

	const vec<float> pos_fraction={	pos.x / (float)qpixmap.width(),
									pos.y / (float)qpixmap.height()};

	if (pos.x < 0 || pos.y < 0 || pos_fraction.x >= 1 || pos_fraction.y >= 1)
		return;

	uint values_in_file[3];
	image_window->processor.get_spot_values(pos_fraction,values_in_file);

	const QRgb screen_rgb=qimage.pixel(pos.x,pos.y);

	if ((e->state() & Qt::ShiftModifier) != 0)
		image_window->add_to_spot_values_clipboard(
								QString::number(values_in_file[0]) + "\t" +
								QString::number(values_in_file[1]) + "\t" +
								QString::number(values_in_file[2]));
	  else {
		char buf[500];
		sprintf(buf,"Values in file: %u/%u/%u\n"
					"RGB on screen: %u/%u/%u",	values_in_file[0],
												values_in_file[1],
												values_in_file[2],
												qRed(screen_rgb),
												qGreen(screen_rgb),
												qBlue(screen_rgb));

		vec<float> rectilinear_angles;
		if (image_window->processor.get_rectilinear_angles(pos_fraction,
													rectilinear_angles))
			sprintf(strchr(buf,'\0'),
					"\n\nAngle from center: %+.2f\xb0 hor, %+.2f\xb0 ver\n"
					"(assuming perfect rectilinear lens)",
								rectilinear_angles.x,rectilinear_angles.y);

		QMessageBox::information(this,MESSAGE_BOX_CAPTION,buf,
							QMessageBox::Ok,QMessageBox::NoButton);
		}
	}

void image_widget_t::resizeEvent(QResizeEvent *)
{
	image_window->check_processing();
	}

image_window_t::image_window_t(QApplication * const app) :
			Q3MainWindow(NULL,"image_window"), processor_t(this),
			fullres_processing_do_resize(0),
			fullres_processing_USM_radius(-1.0f), file_menu(this)
{
	Q3VBox * const qvbox=new Q3VBox(this);
	setCentralWidget(qvbox);

	Q3HBox * const qhbox=new Q3HBox(qvbox);

		/*******************************/
		/*****                     *****/
		/***** normal view widgets *****/
		/*****                     *****/
		/*******************************/

	normal_view_hbox=new Q3HBox(qhbox);
	normal_view_hbox->setSpacing(10);

	contrast_slider=new slider_t(normal_view_hbox,"Contrast",0.5,4,1.3,"%.2fx");
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

	color_balance_view_hbox=new Q3HBox(qhbox);
	color_balance_view_hbox->setSpacing(10);

	red_blue_balance_slider=new two_color_balance_slider_t(
						color_balance_view_hbox,"Red/blue coeffs",0.02,1.7,1);
	connect(red_blue_balance_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	green_balance_slider=new slider_t(color_balance_view_hbox,
											"Green coeff",0.05,1.7,1,"%.2f");
	connect(green_balance_slider->slider,SIGNAL(valueChanged(int)),
									SLOT(color_and_levels_params_changed()));

	grayscale_checkbox=new QCheckBox("Grayscale",color_balance_view_hbox);
	connect(grayscale_checkbox,SIGNAL(toggled(bool)),
									SLOT(color_and_levels_params_changed()));

		/*****************************/
		/*****                   *****/
		/***** crop view widgets *****/
		/*****                   *****/
		/*****************************/

	crop_view_hbox=new Q3HBox(qhbox);
	crop_view_hbox->setSpacing(10);

	crop_target_combobox=new QComboBox((bool)0,crop_view_hbox);
	crop_target_combobox->setMinimumWidth(100);

	{ const QString selected_target_dimensions=settings.readEntry(
								SETTINGS_PREFIX "selected_target_dimensions");

	for (uint i=0;i < lenof(output_dimensions);i++) {
		crop_target_combobox->insertItem(
									output_dimensions[i].display_string());
		if (output_dimensions[i].settings_id() == selected_target_dimensions)
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

		/***********************/
		/*****             *****/
		/***** main window *****/
		/*****             *****/
		/***********************/

	const uint fixed_height=crop_view_hbox->sizeHint().height();
	normal_view_hbox->setFixedHeight(fixed_height);
	color_balance_view_hbox->setFixedHeight(fixed_height);
	crop_view_hbox->setFixedHeight(fixed_height);

	image_widget=new image_widget_t(qvbox,this);
	qvbox->setStretchFactor(image_widget,1000);

	menuBar()->insertItem("&File",&file_menu);

	file_menu_load_save_ids.append(file_menu.insertItem("&Open image..",
					this,SLOT(open_file_dialog()),Qt::CTRL + Qt::Key_O));
	file_menu_load_save_ids.append(file_menu.insertItem(
					"Open &next numbered image",
					this,SLOT(open_next_numbered_image()),Qt::CTRL + Qt::Key_N));
	file_menu_load_save_ids.append(file_menu.insertItem("Save &As..",
					this,SLOT(save_as()),Qt::CTRL + Qt::Key_A));
	file_menu_load_save_ids.append(file_menu.insertItem("&Delete file",
					this,SLOT(delete_file()),Qt::CTRL + Qt::Key_D));
	file_menu.insertItem("E&xit",
					app,SLOT(quit()),Qt::CTRL + Qt::Key_Q);
	file_menu.insertSeparator();

	connect(&file_menu,SIGNAL(activated(int)),SLOT(load_recent_image(int)));

	{ Q3PopupMenu * const view_menu=new Q3PopupMenu(this);
	view_menu->insertItem("&Normal",this,SLOT(select_normal_view()),Qt::Key_F5);
	view_menu->insertItem("Color &Balance",this,
								SLOT(select_color_balance_view()),Qt::Key_F6);
	view_menu->insertItem("&Crop",this,SLOT(select_crop_view()),Qt::Key_F7);
	view_menu->insertItem("Shooting &Info",this,SLOT(shooting_info_dialog()),Qt::CTRL + Qt::Key_I);

	menuBar()->insertItem("&View",view_menu); }

	{ Q3PopupMenu * const help_menu=new Q3PopupMenu(this);
	help_menu->insertItem("&About",this,SLOT(display_help_about()));
	menuBar()->insertItem("&Help",help_menu); }

	select_normal_view();

	processor.set_enh_shadows(0 /*!!!*/);
	color_and_levels_params_changed();
	set_recent_images_in_file_menu();
	}

file_save_options_dialog_t::file_save_options_dialog_t(
				image_window_t * const _image_window,const QString _fname,
												const vec<uint> &resize_size) :
						QDialog(_image_window,"file_save_options_dialog_t",
									TRUE,Qt::WDestructiveClose),
						image_window(_image_window), fname(_fname)
{
	setCaption("Image Save options");

	Q3GridLayout * const grid=new Q3GridLayout(this,3,2,30,30);

	const QString resize_size_str=QString::number(resize_size.x) + "x" +
											QString::number(resize_size.y);

	resize_checkbox=new persistent_checkbox_t(
			"Resize image to " + resize_size_str,this,
			&image_window->settings,SETTINGS_PREFIX "resize_when_saving");
	grid->addMultiCellWidget(resize_checkbox,0,0,0,1);

	Q3HBox * const USM_hbox=new Q3HBox(this);

	unsharp_mask_checkbox=new persistent_checkbox_t(
			"Apply Unsharp Mask with radius",USM_hbox,
			&image_window->settings,SETTINGS_PREFIX "USM_when_saving");

	unsharp_mask_radius_spinbox=new persistent_spinbox_t(1,6,2,
		USM_hbox,&image_window->settings,SETTINGS_PREFIX "USM_radius");

	new QLabel("  pixels",USM_hbox);

	grid->addMultiCellWidget(USM_hbox,1,1,0,1);

	QPushButton * const ok_button=new QPushButton("OK",this);
	ok_button->setFocus();
	ok_button->setDefault(TRUE);
	grid->addWidget(ok_button,2,0);
	connect(ok_button,SIGNAL(clicked(void)),SLOT(accept(void)));

	QPushButton * const cancel_button=new QPushButton("Cancel",this);
	grid->addWidget(cancel_button,2,1);
	connect(cancel_button,SIGNAL(clicked(void)),SLOT(reject(void)));
	}

void file_save_options_dialog_t::accept(void)
{
	image_window->start_fullres_processing(fname,resize_checkbox->isChecked(),
				unsharp_mask_checkbox->isChecked() ?
							unsharp_mask_radius_spinbox->value() : -1.0f);
	QDialog::accept();
	}

void image_window_t::target_dimensions_changed(void)
{
	settings.writeEntry(SETTINGS_PREFIX "selected_target_dimensions",
		output_dimensions[crop_target_combobox->currentItem()].settings_id());
	update_crop_view_label();
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

		if (!file_menu_load_save_ids.contains(i))
			file_menu_load_save_ids.append(i);
		}

	set_caption();
	}

void image_window_t::ensure_fullres_loaded_image(void)
{
#ifndef PHOTOPROC_ALWAYS_USE_HALFRES
	if (!image_file_data.count() || image_fname.isEmpty())
		return;

	sint fd=-1;
	char temp_fname[1000];

	const char * const env_tmpdir=getenv("TMPDIR");
	if (env_tmpdir != NULL) {
		strcpy(temp_fname,env_tmpdir);
		if (strlen(temp_fname))
			if (temp_fname[strlen(temp_fname)-1] != '/')
				strcat(temp_fname,"/");
		strcat(temp_fname,"photoproc-temp-XXXXXX");
		fd=mkstemp(temp_fname);
		}
#ifdef P_tmpdir
	if (fd < 0) {
		strcpy(temp_fname,P_tmpdir "/" "photoproc-temp-XXXXXX");
		fd=mkstemp(temp_fname);
		}
#endif
	if (fd < 0) {
		strcpy(temp_fname,"/tmp/" "photoproc-temp-XXXXXX");
		fd=mkstemp(temp_fname);
		}

	if (fd >= 0) {
		if (write(fd,image_file_data.data(),image_file_data.count()) ==
											(sint)image_file_data.count()) {
			::close(fd);
			image_file_data.resize(0);
			start_loading_image(image_fname,1,temp_fname);
			set_caption();
			return;
			}
		::close(fd);
		}

	image_file_data.resize(0);
	QMessageBox::warning(this,MESSAGE_BOX_CAPTION,
						QString("Could not write temp file ") + temp_fname +
							" for re-loading the image in full resolution. "
							"The image remains loaded in half resolution",
							QMessageBox::Ok,QMessageBox::NoButton);
#endif
	}

void image_window_t::load_image(const QString &fname)
{
	if (processor.operation_pending_count ||
									is_external_reader_process_running())
		return;

	uint is_crop_active=(top_crop->get_value() || bottom_crop->get_value() ||
						left_crop->get_value() || right_crop->get_value());
	const uint window_pixels=size().width() * size().height();
	const uint file_size=QFileInfo(fname).size();

	const QString error_text=start_loading_image(fname,
							is_crop_active || (window_pixels > file_size/3));
	if (!error_text.isNull()) {
		QMessageBox::warning(this,MESSAGE_BOX_CAPTION,error_text,
								QMessageBox::Ok,QMessageBox::NoButton);
		return;
		}

	image_fname=fname;
	set_caption();

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

	set_recent_images_in_file_menu(); }
	}

void image_window_t::crop_params_changed(void)
{
	if (top_crop->get_value() || bottom_crop->get_value() ||
						left_crop->get_value() || right_crop->get_value())
		ensure_fullres_loaded_image();

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

	params.color_coeffs[0]=red_blue_balance_slider->get_value();
	params.color_coeffs[1]=green_balance_slider->get_value();
	params.color_coeffs[2]=two_color_balance_slider_t::get_value2(
									red_blue_balance_slider->get_value());
	params.convert_to_grayscale=grayscale_checkbox->isChecked();

	processor.set_color_and_levels_params(params);
	check_processing();
	}

void image_window_t::set_caption(void)
{
	const char * status=processor.operation_pending_count ?
												"processing..." : "idle";
	if (!processor.operation_pending_count &&
										is_external_reader_process_running())
		status="running external image reader process...";

	setCaption(image_fname + ":  " + status);

		// enable/disable file load/save controls

	{ Q3ValueList<sint>::iterator it;
    for (it=file_menu_load_save_ids.begin();
									it != file_menu_load_save_ids.end();it++)
		file_menu.setItemEnabled(*it,!processor.operation_pending_count &&
									!is_external_reader_process_running()); }
	}

void image_window_t::check_processing(void)
{
	if (processor.operation_pending_count)
		return;

	if (!is_external_reader_process_running()) {
		delete_external_reader_process();

		image_widget->ensure_correct_size();

		if (processor.is_processing_necessary && processor.is_file_loaded)
			processor.start_operation(
								interactive_image_processor_t::PROCESSING);
		}

	set_caption();
	}

bool image_window_t::event(QEvent *e)
{
	if (e->type() != QEvent::User)
		return Q3MainWindow::event(e);

		// operation_completed() event

	if (!start_fullres_processing_fname.isEmpty())
		start_fullres_processing(start_fullres_processing_fname,
				fullres_processing_do_resize,fullres_processing_USM_radius);
	set_caption();

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

			const QString error_text=start_loading_image(fnames.first(),0);
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
		{ load_next(); }
	};

int main(sint argc,char **argv)
{
	Magick::InitializeMagick(NULL);
	QApplication app(argc,argv);

	uint show_only_info=0;
	QStringList fnames;
	for (uint i=1;i < (uint)app.argc();i++) {
		if (app.argv()[i] == QString("-matrix")) {

			if (i+1 < (uint)app.argc()) {
				Magick::PixelPacket dest[14*14];
				const vec<uint> nr_of_patches={14,14};
				Magick::Image img(app.argv()[i+1]);
				color_patches_detector_t detector;
				detector.detect(img,nr_of_patches,dest);
				for (uint i=0;i < 14*14;i++)
					printf("%u,%u,%u\n",
									dest[i].red,dest[i].green,dest[i].blue);
				}
			  else
				optimize_transfer_matrix(stdin);
			return 0;
			}

		if (app.argv()[i] == QString("-info"))
			show_only_info=1;
		  else
			fnames.append(app.argv()[i]);
		}

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
