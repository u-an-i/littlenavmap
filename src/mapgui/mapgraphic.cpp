#include "mapgraphic.h"
#include "mapgui/mapgraphicthread.h"

MapGraphic::MapGraphic( QWidget *parent ) : QWidget(parent) {
    QWidget::setAttribute(Qt::WA_OpaquePaintEvent);
    painter = new QPainter(this);
    paintDisplayAs = &MapGraphic::paintSphere;
    sphereRadiusInPixel = std::min(size().height(), size().width()) * 4.0f / 6.0f / 2.0f;     // earth covers 4/6. of viewport width or height
    //referenceCoverage = 2 * sphereRadiusInPixel / size().width();
    //referenceDistance = size().width() / 2 / sphereRadiusInPixel * earthRadius / tanDefaultLensHalfHorizontalOpeningAngle;
    oldZoom = ceilf(log2f(2 * sphereRadiusInPixel / tileWidth)) + 2;
    netManager = new QNetworkAccessManager(this);
    netManager->setTransferTimeout(3500);
    connect(netManager, &QNetworkAccessManager::finished,
            this, &MapGraphic::netReplyReceived);
}

MapGraphic::~MapGraphic() {
    delete netManager;
    delete painter;
    foreach (QString tileURLForLookup, tilesDownloaded.keys()) {
        QFile file(tileURLForLookup);
        if(file.open(QIODevice::WriteOnly)) {
            QDataStream out(&file);
            out << *tilesDownloaded[tileURLForLookup];
            file.close();
            delete tilesDownloaded[tileURLForLookup];
        }
    }
}

void MapGraphic::paintSphere(QPaintEvent *event) {
    if(doPaint) {
        int regionHeight = event->region().boundingRect().height();
        int amountThreads = QThread::idealThreadCount() - 2;        // - 2 = 1 for the main thread, 1 for the os
        if(amountThreads > regionHeight * scaleFactor) {
            amountThreads = regionHeight * scaleFactor;
        }
        if(amountThreads < 1) {
            amountThreads = 1;
        }
        QList<QThread*> threads;
        QList<threadData*> threadDatas;
        int originalHeight = regionHeight * scaleFactor / amountThreads;
        int counter = 0;
        do {
            threadData *tData = new threadData;
            tData->width = event->region().boundingRect().width() * scaleFactor;
            tData->height = (counter == amountThreads - 1) ? regionHeight * scaleFactor - counter * originalHeight : originalHeight;
            if(tData->width > 0 && tData->height > 0) {
                tData->currentTiles = currentTiles;
                tData->tileWidth = tileWidth;
                tData->tileHeight = tileHeight;
                tData->xStart = event->region().boundingRect().left() * scaleFactor;
                tData->xEnd = tData->xStart + tData->width;
                tData->yStart = event->region().boundingRect().top() * scaleFactor + counter * originalHeight;
                tData->yEnd = tData->yStart + tData->height;
                tData->xOrigin = size().width() / 2 * scaleFactor;
                tData->yOrigin = size().height() / 2 * scaleFactor;
                tData->zoom = ceilf(log2f(2 * sphereRadiusInPixel / tileWidth)) + 2;
                tData->spin = sphereSpin;
                tData->tilt = sphereTilt;
                tData->radius = sphereRadiusInPixel * scaleFactor;
                tData->tryZoomedOut = tData->zoom != oldZoom || tData->spin != oldSpin || tData->tilt != oldTilt;
                tData->indexLastIdRequested = 0;
                tData->indexLastIdCompleted = 0;
                tData->amountFailed = 0;
                if(tData->zoom > zoomMax) {
                    tData->zoom = zoomMax;
                }
                tData->rastering = true;
                threadDatas.append(tData);
                QThread *thread = new MapGraphicThread(tData);
                threads.append(thread);
                thread->start();
            } else {
                delete tData;
            }
        }
        while(++counter < amountThreads);

        QHash<QString, bool> *returnedTiles = new QHash<QString, bool>[threadDatas.count()];
        int countRastering;
        do {
            countRastering = 0;
            int indexTData = 0;
            foreach(threadData *tData, threadDatas) {
                int countQueued = tData->idsMissing.size();
                auto it = tData->idsMissing.begin();
                for(int i=0; i<tData->indexLastIdRequested; ++i) {
                    QString id = *it++;
                    if(!returnedTiles[indexTData].contains(id)) {
                        if(currentTiles->contains(id)) {
                            tData->idsDelivered.push_back(id);
                            returnedTiles[indexTData].insert(id, true);
                            qDebug() << "MGO: thread informed";
                        } else if(failedTiles.contains(id)) {
                            ++tData->amountFailed;
                            returnedTiles[indexTData].insert(id, true);
                            qDebug() << "MGO: thread informed about failure";
                        }
                    }
                }
                for(; tData->indexLastIdRequested < countQueued; ++tData->indexLastIdRequested) {
                    QString id = *it++;
                    QStringList parts = id.split("/");
                    QUrl qUrl = QUrl(QString(tileURL).replace("{z}", parts[0]).replace("{x}", parts[1]).replace("{y}", parts[2]));
                    if(!request2id.contains(qUrl.toString())) {
                        request2id[qUrl.toString()] = id;
                        netManager->get(QNetworkRequest(qUrl));
                        qDebug() << "MGO: image requested " << qUrl.toString();
                    }
                }
                if(tData->rastering) {
                    ++countRastering;
                } else if(tData->image != nullptr) {
                    painter->drawPixmap(event->region().boundingRect().left(), tData->yStart / scaleFactor, QPixmap::fromImage(tData->image->scaled(event->region().boundingRect().width(), (tData->yEnd - tData->yStart) / scaleFactor, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
                    delete tData->image;
                    tData->image = nullptr;
                }
                ++indexTData;
            }
        } while(countRastering > 0);

        delete[] returnedTiles;

        while(threadDatas.count())
            delete threadDatas.takeLast();
        qDebug() << "MGO: about to delete threads";
        while(threads.count())
            delete threads.takeLast();
        qDebug() << "MGO: threads deleted";
    }
}

void MapGraphic::paintRectangle(QPaintEvent *event) {
    if(doPaint) {

    }
}

void MapGraphic::netReplyReceived(QNetworkReply *reply) {
    qDebug() << "MGO: reply received";
    if(reply->error() == QNetworkReply::NoError) {
        QImage image = QImageReader(reply).read();
        qDebug() << "MGO: reply 2 image";
        if(!image.isNull()) {
            currentTiles->insert(request2id[reply->request().url().toString()], image);
            qDebug() << "MGO: image assigned";
            reply->deleteLater();
            qDebug() << "MGO: reply scheduled for deletion";
            return;
        }
    }
    failedTiles.insert(request2id[reply->request().url().toString()], true);
    reply->deleteLater();
}

qreal MapGraphic::distance() const {
    return size().width() / 2 / sphereRadiusInPixel * earthRadius / tanDefaultLensHalfHorizontalOpeningAngle;
}

Marble::MarbleModel *MapGraphic::model() {
    return nullptr;
}

const Marble::MarbleModel *MapGraphic::model() const {
    return nullptr;
}

void MapGraphic::setKeys(QHash<QString, QString> keys) {

}

Marble::RenderStatus MapGraphic::renderStatus() const {
    return Marble::RenderStatus();
}

QPixmap MapGraphic::mapScreenShot() {
    return QPixmap();
}

Marble::Projection MapGraphic::projection() const {
    return Marble::Projection();
}

bool MapGraphic::geoCoordinates( int x, int y,
                                 qreal& lon, qreal& lat,
                                 Marble::GeoDataCoordinates::Unit ) const {
    x = 0.0;
    y = 0.0;
    return true;
}

void MapGraphic::centerOn( const qreal lon, const qreal lat, bool animated ) {

}

Marble::ViewportParams *MapGraphic::viewport() {
    return new Marble::ViewportParams();
}

const Marble::ViewportParams *MapGraphic::viewport() const {
    return new Marble::ViewportParams();
}

void MapGraphic::setMapThemeId( const QString& maptheme ) {
    int tileWidth;
    int tileHeight;
    int zoomMax;
    QString tileURL;
    int found = 0;
    // read, verify and set map theme configuration
    QFile file(maptheme);
    if(file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(QString(file.readAll()));
        file.close();
        bool isTexture = false;
        while (!xml.atEnd()) {
            QStringRef name = xml.name();
            if(isTexture) {
                if(name == "tileSize" && xml.isStartElement()) {
                    tileWidth = xml.attributes().value("width").toInt();
                    found += tileWidth ? 1 : 0;
                    tileHeight = xml.attributes().value("height").toInt();
                    found += tileHeight ? 1 : 0;
                } else if(name == "storageLayout" && xml.isStartElement()) {
                    zoomMax = xml.attributes().value("maximumTileLevel").toInt();
                    if(zoomMax > 30) {
                        zoomMax = 30;
                    }
                    found += zoomMax ? 1 : 0;
                } else if(name == "downloadUrl" && xml.isStartElement()) {
                    tileURL = xml.attributes().value("protocol") + "://" + xml.attributes().value("host") + xml.attributes().value("path");
                    // TODO: verify tileURL is executed by QNetworkAccessManager. Some urls evoke no error, no reply even after timeout passed there.
                    // application would hang for such urls.
                    found += xml.attributes().value("protocol").count() ? 1 : 0;
                    found += xml.attributes().value("host").count() ? 1 : 0;
                    found += xml.attributes().value("path").contains("{z}") ? 1 : 0;
                    found += xml.attributes().value("path").contains("{x}") ? 1 : 0;
                    found += xml.attributes().value("path").contains("{y}") ? 1 : 0;
                } else if(name == "texture") {
                    break;
                }
                xml.readNext();
            } else if(name == "texture") {
                isTexture = true;
                xml.readNext();
            } else {
                xml.readNextStartElement();
            }
        }
    }
    if(found == 8) {
        doPaint = true;
        this->tileWidth = tileWidth;
        this->tileHeight = tileHeight;
        this->zoomMax = zoomMax;
        this->tileURL = tileURL;
        tileURLForLookup = tileURL.replace("/", "_").replace(":", "_").replace("?", "_");
        if(!tilesDownloaded.contains(tileURLForLookup)) {
            QHash<QString, QImage> *hash = new QHash<QString, QImage>();
            QFile file(tileURLForLookup);
            if(file.open(QIODevice::ReadOnly)) {
                QDataStream in(&file);
                in >> *hash;
                file.close();
            }
            tilesDownloaded.insert(tileURLForLookup, hash);
        }
        currentTiles = tilesDownloaded[tileURLForLookup];
        return;
    }
    // TODO: warn about wrong theme configuration
    // inform former theme is still active if one is,
    // on app start nothing is painted
}

void MapGraphic::setShowClouds( bool visible ) {

}

quint64 MapGraphic::volatileTileCacheLimit() const {
    return 0;
}

void MapGraphic::setVolatileTileCacheLimit( quint64 kiloBytes ) {

}

void MapGraphic::setShowSunShading( bool visible ) {

}

void MapGraphic::setSunShadingDimFactor(qreal dimFactor) {

}

bool MapGraphic::showSunShading() const {
    return false;
}

void MapGraphic::setShowPlaces( bool visible ) {

}

void MapGraphic::setShowCities( bool visible ) {

}

void MapGraphic::setShowOtherPlaces( bool visible ) {

}

void MapGraphic::centerOn( const Marble::GeoDataLatLonBox& box, bool animated ) {

}

int MapGraphic::zoom() const {
    return 0;
}

int MapGraphic::zoomStep() const {
    return 1;
}

void MapGraphic::zoomIn( Marble::FlyToMode mode ) {

}

int MapGraphic::maximumZoom() const {
    return 1;
}

void MapGraphic::zoomViewBy( int zoomStep, Marble::FlyToMode mode ) {

}

bool MapGraphic::screenCoordinates( qreal lon, qreal lat,
                       qreal& x, qreal& y ) const {
    return false;
}

void MapGraphic::zoomOut( Marble::FlyToMode mode ) {

}

qreal MapGraphic::centerLatitude() const {
    return 0.0;
}

qreal MapGraphic::centerLongitude() const {
    return 0.0;
}

void MapGraphic::setDistance( qreal distance ) {
    // sphereRadiusInPixel = referenceCoverage / (distance / referenceDistance) * size().width() / 2;
    sphereRadiusInPixel = earthRadius / tanDefaultLensHalfHorizontalOpeningAngle * size().width() / 2 / distance;
}

void MapGraphic::setProjection( Marble::Projection projection ) {
    if(projection == Marble::Projection::Mercator) {
        paintDisplayAs = &MapGraphic::paintSphere;
    } else {
        paintDisplayAs = &MapGraphic::paintRectangle;
    }
}

bool MapGraphic::showGrid() const {
    return false;
}

void MapGraphic::setShowGrid( bool visible ) {

}

bool MapGraphic::showPlaces() const {
    return false;
}

bool MapGraphic::showCities() const {
    return false;
}

bool MapGraphic::showTerrain() const {
    return false;
}

void MapGraphic::setShowTerrain( bool visible ) {

}

bool MapGraphic::showOtherPlaces() const {
    return false;
}

bool MapGraphic::showIceLayer() const {
    return false;
}

void MapGraphic::setShowIceLayer( bool visible ) {

}

QList<Marble::AbstractFloatItem *> MapGraphic::floatItems() const {
    return QList<Marble::AbstractFloatItem *>();
}

void MapGraphic::setMapQualityForViewContext( Marble::MapQuality quality, Marble::ViewContext viewContext ) {

}

void MapGraphic::setAnimationsEnabled( bool enabled ) {

}

void MapGraphic::addLayer( Marble::LayerInterface *layer ) {

}

void MapGraphic::removeLayer( Marble::LayerInterface *layer ) {

}

void MapGraphic::setZoom( int zoom, Marble::FlyToMode mode ) {

}

int MapGraphic::minimumZoom() const {
    return 0;
}

void MapGraphic::resizeEvent( QResizeEvent *event ) {
    tanDefaultLensHalfHorizontalOpeningAngle = event->size().width() / 2 / sphereRadiusInPixel * earthRadius / distance();
}

void MapGraphic::paintEvent( QPaintEvent *event ) {
    (this->*paintDisplayAs)(event);
}

Marble::ViewContext MapGraphic::viewContext() const {
    return Marble::ViewContext();
}

void MapGraphic::setViewContext( Marble::ViewContext viewContext ) {

}

void MapGraphic::moveDown( Marble::FlyToMode mode ) {

}

void MapGraphic::moveLeft( Marble::FlyToMode mode ) {

}

void MapGraphic::moveRight( Marble::FlyToMode mode ) {

}

void MapGraphic::moveUp( Marble::FlyToMode mode ) {

}

Marble::AbstractFloatItem * MapGraphic::floatItem( const QString &nameId ) const {
    return nullptr;
}

void MapGraphic::writePluginSettings( QSettings& settings ) const {

}

QList<Marble::RenderPlugin *> MapGraphic::renderPlugins() const {
    return QList<Marble::RenderPlugin *>();
}

void MapGraphic::setPropertyValue( const QString& name, bool value ) {

}

void MapGraphic::readPluginSettings( QSettings& settings ) {

}

Marble::MarbleWidgetInputHandler *MapGraphic::inputHandler() const {
    return nullptr;
}

void MapGraphic::clearVolatileTileCache() {

}

Marble::TextureLayer *MapGraphic::textureLayer() const {
    return new Marble::TextureLayer(nullptr, nullptr, nullptr, nullptr);
}

void MapGraphic::setRadius( int radius ) {

}

void MapGraphic::zoomView( int zoom, Marble::FlyToMode mode ) {

}

void MapGraphic::rotateBy( const qreal deltaLon, const qreal deltaLat, Marble::FlyToMode mode ) {

}

void MapGraphic::centerOn( const Marble::GeoDataCoordinates &point, bool animated ) {

}

void MapGraphic::centerOn( const Marble::GeoDataPlacemark& placemark, bool animated ) {

}

void MapGraphic::setCenterLatitude( qreal lat, Marble::FlyToMode mode ) {

}

void MapGraphic::setCenterLongitude( qreal lon, Marble::FlyToMode mode ) {

}

void MapGraphic::goHome( Marble::FlyToMode mode ) {

}

void MapGraphic::flyTo( const Marble::GeoDataLookAt &lookAt, Marble::FlyToMode mode ) {

}

void MapGraphic::setProjection( int projection ) {

}

void MapGraphic::setShowOverviewMap( bool visible ) {

}

void MapGraphic::setShowScaleBar( bool visible ) {

}

void MapGraphic::setShowCompass( bool visible ) {

}

void MapGraphic::setShowCityLights( bool visible ) {

}

void MapGraphic::setLockToSubSolarPoint( bool visible ) {

}

void MapGraphic::setSubSolarPointIconVisible( bool visible ) {

}

void MapGraphic::setShowAtmosphere( bool visible ) {

}

void MapGraphic::setShowCrosshairs( bool visible ) {

}

void MapGraphic::setShowRelief( bool visible ) {

}

void MapGraphic::setShowBorders( bool visible ) {

}

void MapGraphic::setShowRivers( bool visible ) {

}

void MapGraphic::setShowLakes( bool visible ) {

}

void MapGraphic::setShowFrameRate( bool visible ) {

}

void MapGraphic::setShowBackground( bool visible ) {

}

void MapGraphic::setShowTileId( bool visible ) {

}

void MapGraphic::setShowRuntimeTrace( bool visible ) {

}

bool MapGraphic::showRuntimeTrace() const {

}

void MapGraphic::setShowDebugPolygons( bool visible) {

}

bool MapGraphic::showDebugPolygons() const {

}

void MapGraphic::creatingTilesStart( Marble::TileCreator *creator, const QString& name, const QString& description ) {

}

void MapGraphic::reloadMap() {

}

void MapGraphic::downloadRegion( QVector<Marble::TileCoordsPyramid> const & ) {

}

void MapGraphic::notifyMouseClick( int x, int y ) {

}

void MapGraphic::setSelection( const QRect& region ) {

}

void MapGraphic::setInputEnabled( bool ) {

}

void MapGraphic::connectNotify(const QMetaMethod &signal) {

}

void MapGraphic::disconnectNotify(const QMetaMethod &signal) {

}

void MapGraphic::leaveEvent( QEvent *event ) {

}

void MapGraphic::changeEvent( QEvent * event ) {

}

void MapGraphic::customPaint( Marble::GeoPainter *painter ) {

}
