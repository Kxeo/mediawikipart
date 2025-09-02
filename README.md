# MediaWiki Viewer KPart (QTextDocument-based)

## Introduction

This repository contains software for the rendered display of MediaWiki documents:

* a MediaWiki viewer [KParts](https://api.kde.org/frameworks/kparts/html/index.html) plugin, which allows KParts-using applications to display files in MediaWiki format in the target format

The software is mainly a wrapper around the classes [QTextDocument](https://doc.qt.io/qt-5/qtextdocument.html) and [QTextBrowser](https://doc.qt.io/qt-5/qtextbrowser.html) from Qt's QWidgets library. The MediaWiki support is thus completely driven by the abilities of those classes.

## Using

To use the MediaWikiPart KParts plugin in a KParts-using applications, often you will need to configure that globally in the Plasma System Settings, and there in the "File Associations" page.
Select the MIME type "text/mediawiki" and in the "Embedding" tab in the "Service Preference Order" group make sure "MediaWiki View (mediawikipart)" is on top of the list.
