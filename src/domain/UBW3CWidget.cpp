
/*
 * UBW3CWidget.cpp
 *
 *  Created on: 7 April 2009
 *      Author: Luc
 */

#include "UBW3CWidget.h"

#include <QtNetwork>
#include <QtXml>

#include "frameworks/UBPlatformUtils.h"
#include "frameworks/UBFileSystemUtils.h"

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "api/UBWidgetUniboardAPI.h"
#include "api/UBWidgetVotingSystemAPI.h"

#include "board/UBBoardController.h"

bool UBW3CWidget::sTemplateLoaded = false;
QMap<QString, QString> UBW3CWidget::sNPAPIWrapperTemplates;
QString UBW3CWidget::sNPAPIWrappperConfigTemplate;


UBW3CWidget::UBW3CWidget(const QUrl& pWidgetUrl, QWidget *parent)
    : UBAbstractWidget(pWidgetUrl, parent)

{
    QString path = pWidgetUrl.toLocalFile();

    QDir potentialDir(path);

    if (!path.endsWith(".wgt") && !path.endsWith(".wgt/") && !potentialDir.exists())
    {
        int lastSlashIndex = path.lastIndexOf("/");
        if (lastSlashIndex > 0)
        {
            path = path.mid(0, lastSlashIndex + 1);
        }
    }

    if(!path.endsWith("/"))
        path += "/";

    int width = 300;
    int height = 150;

    QFile configFile(path + "/config.xml");
    configFile.open(QFile::ReadOnly);

    QDomDocument doc;
    doc.setContent(configFile.readAll());
    QDomNodeList widgetDomList = doc.elementsByTagName("widget");

    if (widgetDomList.count() > 0)
    {
        QDomElement widgetElement = widgetDomList.item(0).toElement();

        width = widgetElement.attribute("width", "300").toInt();
        height = widgetElement.attribute("height", "150").toInt();

        mMetadatas.id = widgetElement.attribute("id", "");

        //some early widget (<= 4.3.4) where using identifier instead of id
        if (mMetadatas.id.length() == 0)
             mMetadatas.id = widgetElement.attribute("identifier", "");

        mMetadatas.version = widgetElement.attribute("version", "");

        // TODO UB 4.x map properly ub namespace
        mIsResizable = widgetElement.attribute("ub:resizable", "false") == "true";
        mIsFreezable = widgetElement.attribute("ub:freezable", "true") == "true";

        QString roles = widgetElement.attribute("ub:roles", "content tool").trimmed().toLower();

        mCanBeTool = roles == "" || roles.contains("tool");
        mCanBeContent =  roles == "" || roles.contains("content");

        QDomNodeList contentDomList = widgetElement.elementsByTagName("content");

        if (contentDomList.count() > 0)
        {
            QDomElement contentElement = contentDomList.item(0).toElement();

            mMainHtmlFileName = contentElement.attribute("src", "");
        }

        mMetadatas.name = textForSubElementByLocale(widgetElement, "name", locale());
        mMetadatas.description = textForSubElementByLocale(widgetElement, "description ", locale());

        QDomNodeList authorDomList = widgetElement.elementsByTagName("author");

        if (authorDomList.count() > 0)
        {
            QDomElement authorElement = authorDomList.item(0).toElement();

            mMetadatas.author = authorElement.text();
            mMetadatas.authorHref = authorElement.attribute("href", "");
            mMetadatas.authorEmail = authorElement.attribute("email ", "");
        }

        QDomNodeList propertiesDomList = widgetElement.elementsByTagName("preference");

        for (uint i = 0; i < propertiesDomList.length(); i++)
        {
            QDomElement preferenceElement = propertiesDomList.at(i).toElement();
            QString prefName = preferenceElement.attribute("name", "");

            if (prefName.length() > 0)
            {
                QString prefValue = preferenceElement.attribute("value", "");
                bool readOnly = (preferenceElement.attribute("readonly", "false") == "true");

                mPreferences.insert(prefName, PreferenceValue(prefValue, readOnly));
            }
        }
    }

    if (mMainHtmlFileName.length() == 0)
    {
        QFile defaultStartFile(path + "index.htm");

        if (defaultStartFile.exists())
        {
            mMainHtmlFileName = "index.htm";
        }
        else
        {
            QFile secondDefaultStartFile(path + "index.html");

            if (secondDefaultStartFile.exists())
            {
                mMainHtmlFileName = "index.html";
            }
        }
    }

    mMainHtmlUrl = pWidgetUrl;
    mMainHtmlUrl.setPath(pWidgetUrl.path() + "/" + mMainHtmlFileName);
    // is it a valid local file ?
    QFile f(mMainHtmlUrl.toLocalFile());

    if(!f.exists())
        mMainHtmlUrl = QUrl(mMainHtmlFileName);

    connect(page()->mainFrame(), SIGNAL(javaScriptWindowObjectCleared()), this, SLOT(javaScriptWindowObjectCleared()));
    connect(UBApplication::boardController, SIGNAL(activeSceneChanged()), this, SLOT(javaScriptWindowObjectCleared()));

    QWebView::load(mMainHtmlUrl);

    setFixedSize(QSize(width, height));

    mNominalSize = QSize(width, height);
}

UBW3CWidget::~UBW3CWidget()
{
    // NOOP
}


void UBW3CWidget::javaScriptWindowObjectCleared()
{
    UBWidgetUniboardAPI *uniboardAPI = new UBWidgetUniboardAPI(UBApplication::boardController->activeScene(), 0);

    page()->mainFrame()->addToJavaScriptWindowObject("uniboard", uniboardAPI);

    UBWidgetVotingSystemAPI *votingSystem = new UBWidgetVotingSystemAPI(this);
    page()->mainFrame()->addToJavaScriptWindowObject("voting", votingSystem);

    connect(votingSystem, SIGNAL(error(const QString&)) , this, SLOT(votingSystemError(const QString&)));
}

void UBW3CWidget::votingSystemError(const QString& error)
{
    page()->mainFrame()->evaluateJavaScript("if(voting.onerror) { voting.onerror('" + error +"');}");

}


bool UBW3CWidget::hasNPAPIWrapper(const QString& pMimeType)
{
    loadNPAPIWrappersTemplates();

    return sNPAPIWrapperTemplates.contains(pMimeType);
}


QString UBW3CWidget::createNPAPIWrapper(const QString& url,
        const QString& pMimeType, const QSize& sizeHint, const QString& pName)
{
    const QString userWidgetPath = UBSettings::settings()->uniboardInteractiveUserDirectory() + "/" + tr("Web");
    QDir userWidgetDir(userWidgetPath);

    return createNPAPIWrapperInDir(url, userWidgetDir, pMimeType, sizeHint, pName);
}



QString UBW3CWidget::createNPAPIWrapperInDir(const QString& pUrl, const QDir& pDir,
    const QString& pMimeType, const QSize& sizeHint,
    const QString& pName)
{
    QString url = pUrl;
    QString name = pName;

    QFileInfo fi(url);

    if (name.length() == 0)
        name = fi.baseName();

    if (fi.exists())
    {
        QString target = pDir.path() + "/" + fi.fileName();
        QFile::copy(url, target);

        url = target;
    }

    loadNPAPIWrappersTemplates();

    QString htmlTemplate;

    if (pMimeType.length() > 0 && sNPAPIWrapperTemplates.contains(pMimeType))
    {
        htmlTemplate = sNPAPIWrapperTemplates.value(pMimeType);
    }
    else
    {
        QString extension = UBFileSystemUtils::extension(url);

        if (sNPAPIWrapperTemplates.contains(extension))
            htmlTemplate = sNPAPIWrapperTemplates.value(extension);
    }

    if (htmlTemplate.length() > 0)
    {
        QString sUrl;

        htmlTemplate = htmlTemplate.replace(QString("{in.url}"), url)
            .replace(QString("{in.width}"), QString("%1").arg(sizeHint.width()))
            .replace(QString("{in.height}"), QString("%1").arg(sizeHint.height()));

        QString configTemplate = sNPAPIWrappperConfigTemplate
            .replace(QString("{in.id}"), url)
            .replace(QString("{in.width}"), QString("%1").arg(sizeHint.width()))
            .replace(QString("{in.height}"), QString("%1").arg(sizeHint.height()))
            .replace(QString("{in.name}"), name)
            .replace(QString("{in.startFile}"), QString("index.htm"));

        QString dirPath = pDir.path();
        if (!pDir.exists())
            pDir.mkpath(dirPath);

        QString widgetLibraryPath = dirPath + "/" + name + ".wgt";
        QDir widgetLibraryDir(widgetLibraryPath);

        if (widgetLibraryDir.exists())
        {
            if (!UBFileSystemUtils::deleteDir(widgetLibraryDir.path()))
            {
                qWarning() << "Cannot delete old widget " << widgetLibraryDir.path();
            }
        }

        widgetLibraryDir.mkpath(widgetLibraryPath);

        QFile configFile(widgetLibraryPath + "/config.xml");

        if (!configFile.open(QIODevice::WriteOnly))
        {
            qWarning() << "Cannot open file " << configFile.fileName();
            return "";
        }

        QTextStream outConfig(&configFile);
        outConfig.setCodec("UTF-8");

        outConfig << configTemplate;
        configFile.close();

        QFile indexFile(widgetLibraryPath + "/index.htm");

        if (!indexFile.open(QIODevice::WriteOnly))
        {
            qWarning() << "Cannot open file " << indexFile.fileName();
            return "";
        }

        QTextStream outIndex(&indexFile);
        outIndex.setCodec("UTF-8");

        outIndex << htmlTemplate;
        indexFile.close();

        return widgetLibraryPath;
    }
    else
    {
        return "";
    }
}


QString UBW3CWidget::createHtmlWrapperInDir(const QString& html, const QDir& pDir,
    const QSize& sizeHint, const QString& pName)
{

    QString widgetPath = pDir.path() + "/" + pName + ".wgt";
    widgetPath = UBFileSystemUtils::nextAvailableFileName(widgetPath);
    QDir widgetDir(widgetPath);

    if (!widgetDir.exists())
    {
        widgetDir.mkpath(widgetDir.path());
    }

    QFile configFile(widgetPath + "/" + "config.xml");

    if (configFile.exists())
    {
        configFile.remove(configFile.fileName());
    }

    if (!configFile.open(QIODevice::WriteOnly))
    {
        qWarning() << "Cannot open file " << configFile.fileName();
        return "";
    }

    QTextStream outConfig(&configFile);
    outConfig.setCodec("UTF-8");
    outConfig << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
    outConfig << "<widget xmlns=\"http://www.w3.org/ns/widgets\"" << endl;
    outConfig << "    xmlns:ub=\"http://uniboard.mnemis.com/widgets\"" << endl;
    outConfig << "    id=\"http://uniboard.mnemis.com/" << pName << "\"" <<endl;

    outConfig << "    version=\"1.0\"" << endl;
    outConfig << "    width=\"" << sizeHint.width() << "\"" << endl;
    outConfig << "    height=\"" << sizeHint.height() << "\"" << endl;
    outConfig << "    ub:resizable=\"true\">" << endl;

    outConfig << "  <name>" << pName << "</name>" << endl;
    outConfig << "  <content src=\"" << pName << ".html\"/>" << endl;

    outConfig << "</widget>" << endl;

    configFile.close();

    const QString fullHtmlFileName = widgetPath + "/" + pName + ".html";

    QFile widgetHtmlFile(fullHtmlFileName);
    if (widgetHtmlFile.exists())
    {
        widgetHtmlFile.remove(widgetHtmlFile.fileName());
    }
    if (!widgetHtmlFile.open(QIODevice::WriteOnly))
    {
        qWarning() << "cannot open file " << widgetHtmlFile.fileName();
        return "";
    }

    QTextStream outStartFile(&widgetHtmlFile);
    outStartFile.setCodec("UTF-8");

    outStartFile << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">" << endl;
    outStartFile << "<html>" << endl;
    outStartFile << "<head>" << endl;
    outStartFile << "    <meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\">" << endl;
    outStartFile << "</head>" << endl;
    outStartFile << "  <body>" << endl;
    outStartFile << html << endl;
    outStartFile << "  </body>" << endl;
    outStartFile << "</html>" << endl;

    widgetHtmlFile.close();

    return widgetPath;

}

void UBW3CWidget::loadNPAPIWrappersTemplates()
{
    if (!sTemplateLoaded)
    {
        sNPAPIWrapperTemplates.clear();

        QString etcPath = UBPlatformUtils::applicationResourcesDirectory() + "/etc/";

        QDir etcDir(etcPath);

        foreach(QString fileName, etcDir.entryList())
        {
            if (fileName.startsWith("npapi-wrapper") && (fileName.endsWith(".htm") || fileName.endsWith(".html")))
            {

                QString htmlContent = UBFileSystemUtils::readTextFile(etcPath + fileName);

                if (htmlContent.length() > 0)
                {
                    QStringList tokens = fileName.split(".");

                    if (tokens.length() >= 4)
                    {
                        QString mime = tokens.at(tokens.length() - 4 );
                        mime += "/" + tokens.at(tokens.length() - 3);

                        QString fileExtension = tokens.at(tokens.length() - 2);

                        sNPAPIWrapperTemplates.insert(mime, htmlContent);
                        sNPAPIWrapperTemplates.insert(fileExtension, htmlContent);
                    }
                }
            }
        }

        sNPAPIWrappperConfigTemplate = UBFileSystemUtils::readTextFile(etcPath + "npapi-wrapper.config.xml");

        sTemplateLoaded = true;
    }
}


QString UBW3CWidget::textForSubElementByLocale(QDomElement rootElement, QString subTagName, QLocale locale)
{
    QDomNodeList subList = rootElement.elementsByTagName(subTagName);

    QString lang = locale.name();

    if (lang.length() > 2)
        lang[2] = QLatin1Char('-');

    if (subList.count() > 1)
    {
        for(int i = 0; i < subList.count(); i++)
        {
            QDomNode node = subList.at(i);
            QDomElement element = node.toElement();

            QString configLang = element.attribute("xml:lang", "");

            if(lang == configLang || (configLang.length() == 2 && configLang == lang.left(2)))
                 return element.text();
        }
    }

    if (subList.count() >= 1)
    {
        QDomElement element = subList.item(0).toElement();
        return element.text();
    }

    return "";
}

