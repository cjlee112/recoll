/* Copyright (C) 2012 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "autoconfig.h"

#include <stdio.h>

#include <string>
#include <vector>
#include <sstream>
using namespace std;

#if defined(USING_WEBKIT)
#include <QWebSettings>
#include <QWebFrame>
#include <QUrl>
#else
#include <QTextBrowser>
#endif
#include <QShortcut>

#include "log.h"
#include "recoll.h"
#include "snippets_w.h"
#include "guiutils.h"
#include "rcldb.h"
#include "rclhelp.h"
#include "plaintorich.h"

// Note: the internal search currently does not work with QTextBrowser. To be
// fixed by looking at the preview code if someone asks for it...
#if defined(USING_WEBKIT)
#define browser ((QWebView*)browserw)
#else
#define browser ((QTextBrowser*)browserw)
#endif

class PlainToRichQtSnippets : public PlainToRich {
public:
    virtual string startMatch(unsigned int)
    {
	return string("<span class='rclmatch' style='")
	    + qs2utf8s(prefs.qtermstyle) + string("'>");
    }
    virtual string endMatch() 
    {
	return string("</span>");
    }
};
static PlainToRichQtSnippets g_hiliter;

void SnippetsW::init()
{
    if (!m_source)
	return;

    QPushButton *searchButton = new QPushButton(tr("Search"));
    searchButton->setAutoDefault(false);
    buttonBox->addButton(searchButton, QDialogButtonBox::ActionRole);

    searchFM->hide();

    new QShortcut(QKeySequence::Find, this, SLOT(slotEditFind()));
    new QShortcut(QKeySequence(Qt::Key_Slash), this, SLOT(slotEditFind()));
    new QShortcut(QKeySequence(Qt::Key_Escape), searchFM, SLOT(hide()));
    new QShortcut(QKeySequence::FindNext, this, SLOT(slotEditFindNext()));
    new QShortcut(QKeySequence(Qt::Key_F3), this, SLOT(slotEditFindNext()));
    new QShortcut(QKeySequence::FindPrevious, this, 
		  SLOT(slotEditFindPrevious()));
    new QShortcut(QKeySequence(Qt::SHIFT + Qt::Key_F3), 
		  this, SLOT(slotEditFindPrevious()));

    QPushButton *closeButton = buttonBox->button(QDialogButtonBox::Close);
    if (closeButton)
	connect(closeButton, SIGNAL(clicked()), this, SLOT(close()));
    connect(searchButton, SIGNAL(clicked()), this, SLOT(slotEditFind()));
    connect(searchLE, SIGNAL(textChanged(const QString&)), 
	    this, SLOT(slotSearchTextChanged(const QString&)));
    connect(nextPB, SIGNAL(clicked()), this, SLOT(slotEditFindNext()));
    connect(prevPB, SIGNAL(clicked()), this, SLOT(slotEditFindPrevious()));

#if defined(USING_WEBKIT)
    browserw = new QWebView(this);
    verticalLayout->insertWidget(0, browserw);
    browser->setUrl(QUrl(QString::fromUtf8("about:blank")));
    connect(browser, SIGNAL(linkClicked(const QUrl &)), 
	    this, SLOT(linkWasClicked(const QUrl &)));
    browser->page()->setLinkDelegationPolicy(QWebPage::DelegateAllLinks);
    browser->page()->currentFrame()->setScrollBarPolicy(Qt::Horizontal,
							Qt::ScrollBarAlwaysOff);
    QWebSettings *ws = browser->page()->settings();
    if (prefs.reslistfontfamily != "") {
	ws->setFontFamily(QWebSettings::StandardFont, prefs.reslistfontfamily);
	ws->setFontSize(QWebSettings::DefaultFontSize, prefs.reslistfontsize);
    }
    if (!prefs.snipCssFile.isEmpty())
	ws->setUserStyleSheetUrl(QUrl::fromLocalFile(prefs.snipCssFile));
#else
    browserw = new QTextBrowser(this);
    verticalLayout->insertWidget(0, browserw);
    connect(browser, SIGNAL(anchorClicked(const QUrl &)), 
	    this, SLOT(linkWasClicked(const QUrl &)));
    browser->setReadOnly(true);
    browser->setUndoRedoEnabled(false);
    browser->setOpenLinks(false);
    browser->setTabChangesFocus(true);
    if (prefs.reslistfontfamily.length()) {
	QFont nfont(prefs.reslistfontfamily, prefs.reslistfontsize);
	browser->setFont(nfont);
    } else {
	browser->setFont(QFont());
    }
#endif

    // Make title out of file name if none yet
    string titleOrFilename;
    string utf8fn;
    m_doc.getmeta(Rcl::Doc::keytt, &titleOrFilename);
    m_doc.getmeta(Rcl::Doc::keyfn, &utf8fn);
    if (titleOrFilename.empty()) {
	titleOrFilename = utf8fn;
    }
    QString title("Recoll - Snippets");
    if (!titleOrFilename.empty()) {
        title += QString(" : ") + QString::fromUtf8(titleOrFilename.c_str());
    }
    setWindowTitle(title);

    vector<Rcl::Snippet> vpabs;
    m_source->getAbstract(m_doc, vpabs);

    HighlightData hdata;
    m_source->getTerms(hdata);

    ostringstream oss;
    oss << 
      "<html><head>"
      "<meta http-equiv=\"content-type\" "
      "content=\"text/html; charset=utf-8\">";

    oss << "<style type=\"text/css\">\nbody,table,select,input {\n";
    oss << "color: " + qs2utf8s(prefs.fontcolor) + ";\n";
    oss << "}\n</style>\n";
    oss << qs2utf8s(prefs.reslistheadertext);

    oss << 
      "</head>"
      "<body>"
      "<table class=\"snippets\">"
      ;

    g_hiliter.set_inputhtml(false);
    bool nomatch = true;

    for (vector<Rcl::Snippet>::const_iterator it = vpabs.begin(); 
	 it != vpabs.end(); it++) {
	if (it->page == -1) {
	    oss << "<tr><td colspan=\"2\">" << 
		it->snippet << "</td></tr>" << endl;
	    continue;
	}
	list<string> lr;
	if (!g_hiliter.plaintorich(it->snippet, lr, hdata)) {
	    LOGDEB1("No match for ["  << (it->snippet) << "]\n" );
	    continue;
	}
	nomatch = false;
	oss << "<tr><td>";
	if (it->page > 0) {
	    oss << "<a href=\"P" << it->page << "T" << it->term << "\">" 
		<< "P.&nbsp;" << it->page << "</a>";
	}
	oss << "</td><td>" << lr.front().c_str() << "</td></tr>" << endl;
    }
    oss << "</table>" << endl;
    if (nomatch) {
	oss.str("<html><head></head><body>\n");
	oss << qs2utf8s(tr("<p>Sorry, no exact match was found within limits. "
                           "Probably the document is very big and the snippets "
                           "generator got lost in a maze...</p>"));
    }
    oss << "\n</body></html>";
#if defined(USING_WEBKIT)
    browser->setHtml(QString::fromUtf8(oss.str().c_str()));
#else
    browser->insertHtml(QString::fromUtf8(oss.str().c_str()));
#endif
}

void SnippetsW::slotEditFind()
{
    searchFM->show();
    searchLE->selectAll();
    searchLE->setFocus();
}

void SnippetsW::slotEditFindNext()
{
    if (!searchFM->isVisible())
	slotEditFind();

#if defined(USING_WEBKIT)
    browser->findText(searchLE->text());
#else
    browser->find(searchLE->text(), 0);
#endif

}
void SnippetsW::slotEditFindPrevious()
{
    if (!searchFM->isVisible())
	slotEditFind();

#if defined(USING_WEBKIT)
    browser->findText(searchLE->text(), QWebPage::FindBackward);
#else
    browser->find(searchLE->text(), QTextDocument::FindBackward);
#endif
}

void SnippetsW::slotSearchTextChanged(const QString& txt)
{
#if defined(USING_WEBKIT)
    browser->findText(txt);
#else
    browser->find(txt, 0);
#endif
}

void SnippetsW::linkWasClicked(const QUrl &url)
{
    string ascurl = (const char *)url.toString().toUtf8();
    LOGDEB("Snippets::linkWasClicked: ["  << (ascurl) << "]\n" );

    if (ascurl.size() > 3) {
	int what = ascurl[0];
	switch (what) {
	case 'P': 
	{
	    string::size_type numpos = ascurl.find_first_of("0123456789");
	    if (numpos == string::npos)
		return;
	    int page = atoi(ascurl.c_str() + numpos);
	    string::size_type termpos = ascurl.find_first_of("T");
	    string term;
	    if (termpos != string::npos)
		term = ascurl.substr(termpos+1);
	    emit startNativeViewer(m_doc, page, 
				   QString::fromUtf8(term.c_str()));
	    return;
	}
	}
    }
    LOGERR("Snippets::linkWasClicked: bad link ["  << (ascurl) << "]\n" );
}


