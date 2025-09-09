/*
    SPDX-FileCopyrightText: 2017, 2020 Friedrich W. H. Kossebau <kossebau@kde.org>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "mediawikipart.hpp"

// part
#include "mediawikiview.hpp"
#include "mediawikibrowserextension.hpp"
#include "searchtoolbar.hpp"
// KF
#include <KPluginMetaData>
#include <KActionCollection>
#include <KStandardAction>
#include <KLocalizedString>
#include <KFileItem>
// Qt
#include <QTextDocument>
#include <QFile>
#include <QTextStream>
#include <QMimeDatabase>
#include <QBuffer>
#include <QShortcut>
#include <QDesktopServices>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QVBoxLayout>


MediaWikiPart::MediaWikiPart(QWidget* parentWidget, QObject* parent, const KPluginMetaData& metaData, Modus modus)
    : KParts::ReadOnlyPart(parent, metaData)
    , m_sourceDocument(new QTextDocument(this))
    , m_widget(new MediaWikiView(m_sourceDocument, parentWidget))
    , m_searchToolBar(new SearchToolBar(m_widget, parentWidget))
    , m_browserExtension(new MediaWikiBrowserExtension(this))
{
    // set internal UI
    auto* mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(m_widget);

    m_searchToolBar->hide();
    mainLayout->addWidget(m_searchToolBar);

    auto* mainWidget = new QWidget(parentWidget);
    mainWidget->setLayout(mainLayout);
    setWidget(mainWidget);

    // set KXMLUI resource file
    setXMLFile(QStringLiteral("mediawikipartui.rc"));

    if (modus == BrowserViewModus) {
        connect(m_widget, &MediaWikiView::anchorClicked,
                m_browserExtension, &MediaWikiBrowserExtension::requestOpenUrl);
        connect(m_widget, &MediaWikiView::copyAvailable,
                m_browserExtension, &MediaWikiBrowserExtension::updateCopyAction);
        connect(m_widget, &MediaWikiView::contextMenuRequested,
                m_browserExtension, &MediaWikiBrowserExtension::requestContextMenu);
    } else {
        connect(m_widget, &MediaWikiView::anchorClicked,
                this, &MediaWikiPart::handleOpenUrlRequest);
        connect(m_widget, &MediaWikiView::contextMenuRequested,
                this, &MediaWikiPart::handleContextMenuRequest);
    }
    connect(m_widget, QOverload<const QUrl &>::of(&MediaWikiView::highlighted),
            this, &MediaWikiPart::showHoveredLink);

    setupActions(modus);
}

MediaWikiPart::~MediaWikiPart() = default;


void MediaWikiPart::setupActions(Modus modus)
{
    // only register to xmlgui if not in browser mode
    QObject* copySelectionActionParent = (modus == BrowserViewModus) ? static_cast<QObject*>(this) : static_cast<QObject*>(actionCollection());
    m_copySelectionAction = KStandardAction::copy(copySelectionActionParent);
    m_copySelectionAction->setText(i18nc("@action", "&Copy Text"));
    m_copySelectionAction->setEnabled(m_widget->hasSelection());
    connect(m_widget, &MediaWikiView::copyAvailable,
            m_copySelectionAction, &QAction::setEnabled);
    connect(m_copySelectionAction, &QAction::triggered, this, &MediaWikiPart::copySelection);

    m_selectAllAction = KStandardActions::selectAll(this, &MediaWikiPart::selectAll, actionCollection());
    m_selectAllAction->setShortcutContext(Qt::WidgetShortcut);
    m_widget->addAction(m_selectAllAction);

    m_searchAction = KStandardActions::find(m_searchToolBar, &SearchToolBar::startSearch, actionCollection());
    m_searchAction->setEnabled(false);
    m_widget->addAction(m_searchAction);

    m_searchNextAction = KStandardActions::findNext(m_searchToolBar, &SearchToolBar::searchNext, actionCollection());
    m_searchNextAction->setEnabled(false);
    m_widget->addAction(m_searchNextAction);

    m_searchPreviousAction = KStandardActions::findPrev(m_searchToolBar, &SearchToolBar::searchPrevious, actionCollection());
    m_searchPreviousAction->setEnabled(false);
    m_widget->addAction(m_searchPreviousAction);

    auto* closeFindBarShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), widget());
    closeFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeFindBarShortcut, &QShortcut::activated, m_searchToolBar, &SearchToolBar::hide);
}

bool MediaWikiPart::openFile()
{
    QFile file(localFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    prepareViewStateRestoringOnReload();

    QTextStream stream(&file);
    QString text = stream.readAll();

    file.close();

    m_sourceDocument->setMediaWiki(text);
    const QUrl b = QUrl::fromLocalFile(localFilePath()).adjusted(QUrl::RemoveFilename);
    m_sourceDocument->setBaseUrl(b);

    restoreScrollPosition();

    m_searchAction->setEnabled(true);
    m_searchNextAction->setEnabled(true);
    m_searchPreviousAction->setEnabled(true);

    return true;
}

bool MediaWikiPart::doOpenStream(const QString& mimeType)
{
    const QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType);
    if (!mime.inherits(QStringLiteral("text/mediawiki"))) {
        return false;
    }

    m_streamedData.clear();
    m_sourceDocument->setMediaWiki(QString());
    return true;
}

bool MediaWikiPart::doWriteStream(const QByteArray& data)
{
    m_streamedData.append(data);
    return true;
}

bool MediaWikiPart::doCloseStream()
{
    QBuffer buffer(&m_streamedData);

    if (!buffer.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_streamedData.clear();
        return false;
    }

    prepareViewStateRestoringOnReload();

    QTextStream stream(&buffer);
    QString text = stream.readAll();

    m_sourceDocument->setMediaWiki(text);
    m_sourceDocument->setBaseUrl(QUrl());

    restoreScrollPosition();

    m_searchAction->setEnabled(true);
    m_searchNextAction->setEnabled(true);
    m_searchPreviousAction->setEnabled(true);

    m_streamedData.clear();
    return true;
}

bool MediaWikiPart::closeUrl()
{
    // protect against repeated call if already closed
    const QUrl currentUrl = url();
    if (currentUrl.isValid()) {
        m_previousScrollPosition = m_widget->scrollPosition();
        m_previousUrl = currentUrl;
    }

    m_sourceDocument->setMediaWiki(QString());
    m_sourceDocument->setBaseUrl(QUrl());
    m_searchAction->setEnabled(false);
    m_searchNextAction->setEnabled(false);
    m_searchPreviousAction->setEnabled(false);
    m_streamedData.clear();

    return ReadOnlyPart::closeUrl();
}

void MediaWikiPart::prepareViewStateRestoringOnReload()
{
    if (url() == m_previousUrl) {
        KParts::OpenUrlArguments args(arguments());
        args.setXOffset(m_previousScrollPosition.x());
        args.setYOffset(m_previousScrollPosition.y());
        setArguments(args);
    }
}

void MediaWikiPart::restoreScrollPosition()
{
    const KParts::OpenUrlArguments args(arguments());
    m_widget->setScrollPosition({args.xOffset(), args.yOffset()});
}

void MediaWikiPart::handleOpenUrlRequest(const QUrl& url)
{
    QDesktopServices::openUrl(url);
}

void MediaWikiPart::handleContextMenuRequest(QPoint globalPos,
                                            const QUrl& linkUrl,
                                            bool hasSelection)
{
    QMenu menu(m_widget);

    if (!linkUrl.isValid()) {
        if (hasSelection) {
            menu.addAction(m_copySelectionAction);
        } else {
            menu.addAction(m_selectAllAction);
            if (m_searchToolBar->isHidden()) {
                menu.addAction(m_searchAction);
            }
        }
    } else {
        QAction* action = menu.addAction(i18nc("@action", "Open Link"));
        connect(action, &QAction::triggered, this, [&] {
            handleOpenUrlRequest(linkUrl);
        });
        menu.addSeparator();

        if (linkUrl.scheme() == QLatin1String("mailto")) {
            menu.addAction(createCopyEmailAddressAction(&menu, linkUrl));
        } else {
            menu.addAction(createCopyLinkUrlAction(&menu, linkUrl));
        }
    }

    if (!menu.isEmpty()) {
        menu.exec(globalPos);
    }
}

void MediaWikiPart::showHoveredLink(const QUrl& _linkUrl)
{
    QUrl linkUrl = resolvedUrl(_linkUrl);
    QString message;
    KFileItem fileItem;

    if (linkUrl.isValid()) {

        // Protect the user against URL spoofing!
        linkUrl.setUserName(QString());
        message = linkUrl.toDisplayString();

        if (linkUrl.scheme() != QLatin1String("mailto")) {
            fileItem = KFileItem(linkUrl, QString(), KFileItem::Unknown);
        }
    }

    Q_EMIT m_browserExtension->mouseOverInfo(fileItem);
    Q_EMIT setStatusBarText(message);
}

QAction* MediaWikiPart::copySelectionAction() const
{
    return m_copySelectionAction;
}

QAction* MediaWikiPart::createCopyEmailAddressAction(QObject* parent, const QUrl& mailtoUrl)
{
    auto* action = new QAction(parent);
    action->setText(i18nc("@action", "&Copy Email Address"));
    connect(action, &QAction::triggered, parent, [&] {
        auto* data = new QMimeData;
        data->setText(mailtoUrl.path());
        QApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    });

    return action;
}

QAction* MediaWikiPart::createCopyLinkUrlAction(QObject* parent, const QUrl& linkUrl)
{
    auto* action = new QAction(parent);
    action->setText(i18nc("@action", "Copy Link &URL"));
    connect(action, &QAction::triggered, parent, [&] {
        auto* data = new QMimeData;
        data->setUrls({linkUrl});
        QApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    });

    return action;
}

void MediaWikiPart::copySelection()
{
    m_widget->copy();
}

void MediaWikiPart::selectAll()
{
    m_widget->selectAll();
}

QUrl MediaWikiPart::resolvedUrl(const QUrl &url) const
{
    QUrl u = url;
    if (u.isRelative()) {
        const QUrl baseUrl = m_sourceDocument->baseUrl();
        u = baseUrl.resolved(u);
    }

    return (u.adjusted(QUrl::NormalizePathSegments));
}

#include "moc_mediawikipart.cpp"
