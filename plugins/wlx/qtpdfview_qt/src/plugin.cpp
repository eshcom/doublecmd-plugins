#include <QtPdf>
#include <QPdfView>
#include <QtWidgets>

#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>

#include "wlxplugin.h"

#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"

#define CFG_CONTROLS PLUGNAME"/controls"
#define CFG_SINGLEPAGE PLUGNAME"/single_page"
#define CFG_FITTOWIDTH PLUGNAME"/fit_to_width"
#if QT_VERSION >= 0x060000
#define NoError Error::None
#define CustomZoom Custom
#define pageCount() document()->pageCount()
#define navigator pageNavigator
#define QPdfPageNavigation QPdfPageNavigator
#else
#define navigator pageNavigation
#define pageCount() pageNavigation()->pageCount()
#endif

static bool gControls = true;
static bool gSinglePage = false;
static bool gFitToWidth = true;

static void update_pagecount(QLabel *label, QPdfView *view)
{
	int total = view->pageCount();
	int page = view->navigator()->currentPage() + 1;
	label->setText(QString("%1/%2").arg(page).arg(total));
}

HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	QPdfDocument *document = new QPdfDocument();

	if (document->load(QString(FileToLoad)) != QPdfDocument::NoError)
	{
		delete document;
		return nullptr;
	}

	QFrame *view = new QFrame((QWidget*)ParentWin);
	view->setFrameStyle(QFrame::NoFrame);
	QVBoxLayout *main = new QVBoxLayout(view);
	QToolBar *controls = new QToolBar(view);

	QPdfView *pdfView = new QPdfView(view);
	main->setSpacing(0);
	main->setContentsMargins(1, 1, 1, 1);
	main->addWidget(controls);
	main->addWidget(pdfView);

	QAction *actFirst = new QAction(QIcon::fromTheme("go-first"), _("First page"), view);
	actFirst->setShortcut(QKeySequence::MoveToStartOfDocument);
	QObject::connect(actFirst, &QAction::triggered, [pdfView]()
	{
#if QT_VERSION >= 0x060000
		pdfView->navigator()->jump(0,  pdfView->navigator()->currentLocation());
#else
		pdfView->navigator()->setCurrentPage(0);
#endif
	});
	controls->addAction(actFirst);

	QAction *actPrev = new QAction(QIcon::fromTheme("go-previous"), _("Previous page"), view);
	actPrev->setShortcut(QKeySequence::MoveToPreviousPage);
	QObject::connect(actPrev, &QAction::triggered, [pdfView]()
	{
#if QT_VERSION >= 0x060000
		int page = pdfView->navigator()->currentPage();

		if (page > 0)
			pdfView->navigator()->jump(page - 1,  pdfView->navigator()->currentLocation());
#else
		pdfView->navigator()->goToPreviousPage();
#endif
	});
	controls->addAction(actPrev);

	QAction *actNext = new QAction(QIcon::fromTheme("go-next"), _("Next page"), view);
	actNext->setShortcut(QKeySequence::MoveToNextPage);
	QObject::connect(actNext, &QAction::triggered, [pdfView]()
	{
#if QT_VERSION >= 0x060000
		int page = pdfView->navigator()->currentPage();

		if (page < pdfView->pageCount() - 1)
			pdfView->navigator()->jump(page + 1,  pdfView->navigator()->currentLocation());
#else
		pdfView->navigator()->goToNextPage();
#endif
	});
	controls->addAction(actNext);

	QAction *actLast = new QAction(QIcon::fromTheme("go-last"), _("Last page"), view);
	actLast->setShortcut(QKeySequence::MoveToEndOfDocument);
	QObject::connect(actLast, &QAction::triggered, [pdfView]()
	{
		int pages = pdfView->pageCount();

		if (pages > 0)
#if QT_VERSION >= 0x060000
			pdfView->navigator()->jump(pages - 1, pdfView->navigator()->currentLocation());
#else
			pdfView->navigator()->setCurrentPage(pages - 1);
#endif
	});
	controls->addAction(actLast);

	controls->addSeparator();
	QLabel *lblPages = new QLabel(view);
#if QT_VERSION >= 0x060000
	QObject::connect(pdfView, &QPdfView::documentChanged, [lblPages, pdfView]()
	{
		update_pagecount(lblPages, pdfView);
	});

	QObject::connect(document, &QPdfDocument::pageCountChanged, [lblPages, pdfView]()
	{
		update_pagecount(lblPages, pdfView);
	});
#else
	QObject::connect(pdfView->navigator(), &QPdfPageNavigation::pageCountChanged, [lblPages, pdfView]()
	{
		update_pagecount(lblPages, pdfView);
	});
#endif
	QObject::connect(pdfView->navigator(), &QPdfPageNavigation::currentPageChanged, [lblPages, pdfView]()
	{
		update_pagecount(lblPages, pdfView);
	});
	controls->addWidget(lblPages);
	QAction *actGoTo = new QAction(QIcon::fromTheme("go-jump"), _("Go to..."), view);
	actGoTo->setShortcut(QKeySequence("Ctrl+G"));
	QObject::connect(actGoTo, &QAction::triggered, [pdfView]()
	{
		int pages = pdfView->pageCount();
		int cur = pdfView->navigator()->currentPage() + 1;

		bool ret;
		int page = QInputDialog::getInt(pdfView, "", _("Page number to go to:"), cur, 1, pages, 1, &ret);

		if (ret)
#if QT_VERSION >= 0x060000
			pdfView->navigator()->jump(page - 1, pdfView->navigator()->currentLocation());
#else
			pdfView->navigator()->setCurrentPage(page - 1);
#endif
	});
	controls->addAction(actGoTo);

	controls->addSeparator();

	QAction *actZoomIn = new QAction(QIcon::fromTheme("zoom-in"), _("Zoom In"), view);
	actZoomIn->setShortcut(QKeySequence::ZoomIn);
	QObject::connect(actZoomIn, &QAction::triggered, [pdfView]()
	{
		if (pdfView->zoomMode() != QPdfView::ZoomMode::CustomZoom)
			pdfView->setZoomMode(QPdfView::ZoomMode::CustomZoom);

		pdfView->setZoomFactor(pdfView->zoomFactor() + 0.05);
	});
	controls->addAction(actZoomIn);

	QAction *actZoomOut = new QAction(QIcon::fromTheme("zoom-out"), _("Zoom Out"), view);
	actZoomOut->setShortcut(QKeySequence::ZoomOut);
	QObject::connect(actZoomOut, &QAction::triggered, [pdfView]()
	{
		if (pdfView->zoomMode() != QPdfView::ZoomMode::CustomZoom)
			pdfView->setZoomMode(QPdfView::ZoomMode::CustomZoom);

		pdfView->setZoomFactor(pdfView->zoomFactor() - 0.05);
	});
	controls->addAction(actZoomOut);

	QAction *actZoomOrg = new QAction(QIcon::fromTheme("zoom-original"), _("Original Size"), view);
	actZoomOrg->setShortcut(QKeySequence("Ctrl+0"));
	QObject::connect(actZoomOrg, &QAction::triggered, [pdfView]()
	{
		if (pdfView->zoomMode() != QPdfView::ZoomMode::CustomZoom)
			pdfView->setZoomMode(QPdfView::ZoomMode::CustomZoom);

		pdfView->setZoomFactor(1);
	});
	controls->addAction(actZoomOrg);

	controls->addSeparator();

	QAction *actFit = new QAction(QIcon::fromTheme("zoom-fit-best"), _("Fit"), view);
	actFit->setShortcut(QKeySequence("Shift+M"));
	actFit->setCheckable(true);
	QObject::connect(actFit, &QAction::triggered, [pdfView]()
	{
		if (pdfView->zoomMode() == QPdfView::ZoomMode::FitInView)
			pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
		else
			pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
	});
	controls->addAction(actFit);

	QAction *actPageMode = new QAction(QIcon::fromTheme("document-page-setup"), _("Page Mode"), view);
	actPageMode->setShortcut(QKeySequence("Ctrl+M"));
	actPageMode->setCheckable(true);
	QObject::connect(actPageMode, &QAction::triggered, [pdfView]()
	{
		if (pdfView->pageMode() != QPdfView::PageMode::MultiPage)
			pdfView->setPageMode(QPdfView::PageMode::MultiPage);
		else
			pdfView->setPageMode(QPdfView::PageMode::SinglePage);
	});
	controls->addAction(actPageMode);

	controls->addSeparator();

	QAction *actInfo = new QAction(QIcon::fromTheme("dialog-information"), _("Info"), view);
	actInfo->setShortcut(QKeySequence("Shift+F1"));
	QObject::connect(actInfo, &QAction::triggered, [pdfView]()
	{
		QString info;
		QMap<QString, QPdfDocument::MetaDataField> fields;
		fields[_("Author")] = QPdfDocument::MetaDataField::Author;
		fields[_("Title")] = QPdfDocument::MetaDataField::Title;
		fields[_("Subject")] = QPdfDocument::MetaDataField::Subject;
		fields[_("Producer")] = QPdfDocument::MetaDataField::Producer;
		fields[_("Creator")] = QPdfDocument::MetaDataField::Creator;
		fields[_("Keywords")] = QPdfDocument::MetaDataField::Keywords;
		fields[_("Creation Date")] = QPdfDocument::MetaDataField::CreationDate;
		fields[_("Modification Date")] = QPdfDocument::MetaDataField::ModificationDate;
		QMapIterator<QString, QPdfDocument::MetaDataField> i(fields);

		while (i.hasNext())
		{
			i.next();
			QString value = pdfView->document()->metaData(i.value()).toString();

			if (!value.isEmpty())
			{
				info.append(i.key());
				info.append(": ");
				info.append(value);
				info.append("\n");
			}
		}

		if (!info.isEmpty())
			QMessageBox::information(pdfView, "", info);
		else
			QMessageBox::information(pdfView, "", _("no suitable info available"));
	});
	controls->addAction(actInfo);

	pdfView->setDocument(document);

	if (gSinglePage)
		pdfView->setPageMode(QPdfView::PageMode::SinglePage);
	else
		pdfView->setPageMode(QPdfView::PageMode::MultiPage);

	if (gFitToWidth)
		pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);

	pdfView->setObjectName("pdf_view");
	view->setFocusProxy(pdfView);
	view->show();

	if (!gControls)
		controls->hide();

	return view;
}

int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags)
{
	QFrame *view = (QFrame*)ParentWin;
	QPdfView *pdfView = view->findChild<QPdfView*>("pdf_view");
	QPdfDocument *document = pdfView->document();

	if (document != NULL)
	{
		document->close();

		if (document->load(QString(FileToLoad)) != QPdfDocument::NoError)
			return LISTPLUGIN_ERROR;

		return LISTPLUGIN_OK;
	}

	return LISTPLUGIN_ERROR;
}

void DCPCALL ListCloseWindow(HANDLE ListWin)
{
	QFrame *view = (QFrame*)ListWin;
	QPdfView *pdfView = view->findChild<QPdfView*>("pdf_view");
	QPdfDocument *document = pdfView->document();

	if (document != NULL)
	{
		document->close();
		delete document;
	}

	delete pdfView;
	delete view;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	snprintf(DetectString, maxlen - 1, "EXT=\"PDF\"");
}

int DCPCALL ListSearchDialog(HWND ListWin, int FindNext)
{
	return LISTPLUGIN_OK;
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	Dl_info dlinfo;
	static char plg_path[PATH_MAX];
	const char* loc_dir = "langs";

	memset(&dlinfo, 0, sizeof(dlinfo));

	if (dladdr(plg_path, &dlinfo) != 0)
	{
		strncpy(plg_path, dlinfo.dli_fname, PATH_MAX);
		char *pos = strrchr(plg_path, '/');

		if (pos)
			strcpy(pos + 1, loc_dir);

		setlocale(LC_ALL, "");
		bindtextdomain(GETTEXT_PACKAGE, plg_path);
		textdomain(GETTEXT_PACKAGE);
	}

	QFileInfo defini(QString::fromStdString(dps->DefaultIniName));
	QString cfgpath = defini.absolutePath() + "/j2969719.ini";
	QSettings settings(cfgpath, QSettings::IniFormat);

	if (!settings.contains(CFG_CONTROLS))
		settings.setValue(CFG_CONTROLS, gControls);
	else
		gControls = settings.value(CFG_CONTROLS).toBool();

	if (!settings.contains(CFG_SINGLEPAGE))
		settings.setValue(CFG_SINGLEPAGE, gSinglePage);
	else
		gSinglePage = settings.value(CFG_SINGLEPAGE).toBool();

	if (!settings.contains(CFG_FITTOWIDTH))
		settings.setValue(CFG_FITTOWIDTH, gFitToWidth);
	else
		gFitToWidth = settings.value(CFG_FITTOWIDTH).toBool();
}
