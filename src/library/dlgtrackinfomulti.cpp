#include "library/dlgtrackinfomulti.h"

#include <QLineEdit>
#include <QStyleFactory>
#include <QtDebug>

#include "defs_urls.h"
#include "library/coverartcache.h"
#include "library/coverartutils.h"
#include "library/library_prefs.h"
#include "moc_dlgtrackinfomulti.cpp"
#include "preferences/colorpalettesettings.h"
#include "sources/soundsourceproxy.h"
#include "track/beatutils.h"
#include "track/track.h"
#include "util/color/color.h"
#include "util/datetime.h"
#include "util/duration.h"
#include "util/optional.h"
#include "util/stringformat.h"
#include "widget/wcoverartlabel.h"
#include "widget/wcoverartmenu.h"
#include "widget/wstarrating.h"

namespace {

const QString kVariousText = QChar('<') + QObject::tr("various") + QChar('>');
const char* kOrigValProp = "origVal";
const QString kClearItem = QStringLiteral("clearItem");

/// If value differs from the current value, add it to the list.
/// If new and current are identical, keep only one. Later on we can use the
/// item count to maybe join the list and format the font accordingly.
template<typename T>
void addToTagSet(QSet<T>* pList, const T& value) {
    if (pList->isEmpty() || !pList->contains(value)) {
        pList->insert(value);
    }
}

void setItalic(QWidget* pEditor, bool italic) {
    auto font = pEditor->font();
    if (font.italic() == italic) {
        return;
    }
    font.setItalic(italic);
    pEditor->setFont(font);
}

void setBold(QWidget* pEditor, bool bold) {
    auto font = pEditor->font();
    if (font.bold() == bold) {
        return;
    }
    font.setBold(bold);
    pEditor->setFont(font);
}

/// Check if the text has been edited, i.e. is not <various>
QString validEditText(QComboBox* pBox) {
    QString origVal = pBox->property(kOrigValProp).toString();
    if (pBox->currentIndex() == -1 &&
            (pBox->lineEdit()->text() == origVal ||
                    pBox->lineEdit()->placeholderText() == kVariousText)) {
        // This is either a single-value box and the value changed, or this is a
        // multi-value box and the placeholder text was removed when clearing it.
        return QString();
    }
    // We have a new text
    return pBox->currentText().trimmed();
}

/// Sets the text of a QLabel, either the only value or the 'various' string.
/// In case of `various`, the text is also set italic.
/// This is used for bitrate, sample rate and file directories.
/// Optionally toggle bold (bitrate and sample rate).
template<typename T>
void setCommonValueOrVariousStringAndFormatFont(QLabel* pLabel,
        QSet<T>& values,
        bool toggleBold = false,
        const QString& unit = QString()) {
    if (values.size() == 1) {
        QString text = convertToQStringConvertible(*values.constBegin());
        if (text.isNull()) {
            pLabel->clear();
            return;
        }
        if (!unit.isEmpty()) {
            text.append(QChar(' ') + unit);
        }
        pLabel->setText(text);
        setItalic(pLabel, false);
        if (toggleBold) {
            setBold(pLabel, true);
        }
    } else {
        pLabel->setText(kVariousText);
        setItalic(pLabel, true);
        if (toggleBold) {
            setBold(pLabel, false);
        }
    }
}

} // namespace

DlgTrackInfoMulti::DlgTrackInfoMulti(UserSettingsPointer pUserSettings)
        // No parent because otherwise it inherits the style parent's
        // style which can make it unreadable. Bug #673411
        : QDialog(nullptr),
          m_pUserSettings(std::move(pUserSettings)),
          m_pWCoverArtMenu(make_parented<WCoverArtMenu>(this)),
          m_pWCoverArtLabel(make_parented<WCoverArtLabel>(this, m_pWCoverArtMenu)),
          m_pWStarRating(make_parented<WStarRating>(this)),
          m_starRatingModified(false),
          m_newRating(0),
          m_colorChanged(false),
          m_newColor(mixxx::RgbColor::nullopt()),
          m_pColorPicker(make_parented<WColorPickerAction>(
                  WColorPicker::Option::AllowNoColor |
                          // TODO(xxx) remove this once the preferences are themed via QSS
                          WColorPicker::Option::NoExtStyleSheet,
                  ColorPaletteSettings(m_pUserSettings).getTrackColorPalette(),
                  this)) {
    init();
}

void DlgTrackInfoMulti::init() {
    setupUi(this);
    setWindowIcon(QIcon(MIXXX_ICON_PATH));

    // QDialog buttons
    connect(btnApply,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::slotApply);

    connect(btnOK,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::slotOk);

    connect(btnCancel,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::slotCancel);

    connect(btnReset,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::updateFromTracks);

    connect(btnImportMetadataFromFile,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::slotImportMetadataFromFiles);

    QList<QComboBox*> valueComboBoxes;
    valueComboBoxes.append(txtArtist);
    valueComboBoxes.append(txtTitle);
    valueComboBoxes.append(txtAlbum);
    valueComboBoxes.append(txtAlbumArtist);
    valueComboBoxes.append(txtComposer);
    valueComboBoxes.append(txtGenre);
    valueComboBoxes.append(txtYear);
    valueComboBoxes.append(txtKey);
    valueComboBoxes.append(txtTrackNumber);
    valueComboBoxes.append(txtGrouping);

    for (QComboBox* pBox : valueComboBoxes) {
        // This will be displayed if there are multiple values
        pBox->setEditable(true);
        // We allow editing the value but we don't want to add each edit to the item list
        pBox->setInsertPolicy(QComboBox::NoInsert);

        connect(pBox,
                &QComboBox::currentIndexChanged,
                [pBox]() {
                    // If we have multiple value we also added the Clear All item.
                    // If the Clear item has been selected, remove the placeholder
                    // in order to have a safe indicator in validEditText() whether
                    // the box has been edited.
                    auto data = pBox->currentData(Qt::UserRole);
                    if (data.isValid() && data.toString() == kClearItem) {
                        pBox->lineEdit()->setPlaceholderText(QString());
                        pBox->setCurrentIndex(-1); // This clears the edit text
                        // Remove the Clear item afte use. If required, it's added
                        // as first item.
                        pBox->removeItem(0);
                    }
                });
    }
    // Note: unlike other tags, comments can be multi-line, though while QComboBox
    // can have multi-line items its Q*Line*Edit is not suitable for editing multi-
    // line content. In order to get the same UX for comments like we have for
    // regular tags, the two buddies require a special setup:
    // * txtCommentBox is not editable
    // * if an item is selected in txtCommentBox, the text is shown in txtComment
    // * for multiple values, we show the <various> placeholder also in txtCommentBox
    // This also requires some special handling in saveTracks().
    txtCommentBox->setInsertPolicy(QComboBox::NoInsert);
    connect(txtCommentBox,
            &QComboBox::currentIndexChanged,
            this,
            [this]() {
                txtCommentBox->blockSignals(true);
                txtComment->setPlaceholderText(QString());
                // If we have multiple value we also added the Clear All item.
                // If the Clear item has been selected, remove the placeholder
                // in order to have a safe indicator in validEditText() whether
                // the box has been edited.
                setItalic(txtComment, false);
                auto data = txtCommentBox->currentData(Qt::UserRole);
                if (data.isValid() && data.toString() == kClearItem) {
                    txtCommentBox->setCurrentIndex(-1); // This clears the edit text
                    // Remove the Clear item afte use. If required, it's added
                    // as first item.
                    txtCommentBox->removeItem(0);
                    txtComment->clear();
                } else {
                    txtComment->setPlainText(txtCommentBox->currentText());
                }
                txtCommentBox->blockSignals(false);
            });

    // Set up key validation, i.e. check manually entered key texts
    connect(txtKey->lineEdit(),
            &QLineEdit::editingFinished,
            this,
            &DlgTrackInfoMulti::slotKeyTextChanged);

    btnColorPicker->setStyle(QStyleFactory::create(QStringLiteral("fusion")));
    QMenu* pColorPickerMenu = new QMenu(this);
    pColorPickerMenu->addAction(m_pColorPicker);
    btnColorPicker->setMenu(pColorPickerMenu);

    connect(btnColorPicker,
            &QPushButton::clicked,
            this,
            &DlgTrackInfoMulti::slotColorButtonClicked);
    connect(m_pColorPicker.get(),
            &WColorPickerAction::colorPicked,
            this,
            &DlgTrackInfoMulti::slotColorPicked);

    // Insert the star rating widget
    starsLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    starsLayout->setSpacing(0);
    starsLayout->setContentsMargins(0, 0, 0, 0);
    starsLayout->insertWidget(0, m_pWStarRating.get());
    // This is necessary to pass on mouseMove events to WStarRating
    m_pWStarRating->setMouseTracking(true);
    connect(m_pWStarRating,
            &WStarRating::ratingChangeRequest,
            this,
            &DlgTrackInfoMulti::slotStarRatingChanged);

    // Insert the cover widget
    coverLayout->setAlignment(Qt::AlignRight | Qt::AlignTop);
    coverLayout->setSpacing(0);
    coverLayout->setContentsMargins(0, 0, 0, 0);
    coverLayout->insertWidget(0, m_pWCoverArtLabel.get());
    CoverArtCache* pCache = CoverArtCache::instance();
    if (pCache) {
        connect(pCache,
                &CoverArtCache::coverFound,
                this,
                &DlgTrackInfoMulti::slotCoverFound);
    }

    connect(m_pWCoverArtMenu,
            &WCoverArtMenu::coverInfoSelected,
            this,
            &DlgTrackInfoMulti::slotCoverInfoSelected);

    connect(m_pWCoverArtMenu,
            &WCoverArtMenu::reloadCoverArt,
            this,
            &DlgTrackInfoMulti::slotReloadCoverArt);
}

void DlgTrackInfoMulti::slotApply() {
    saveTracks();
}

void DlgTrackInfoMulti::slotOk() {
    slotApply();
    clear();
    accept();
}

void DlgTrackInfoMulti::slotCancel() {
    clear();
    reject();
}

void DlgTrackInfoMulti::loadTracks(const QList<TrackPointer>& pTracks) {
    clear();

    if (pTracks.isEmpty()) {
        return;
    }

    m_pLoadedTracks.clear();
    for (const auto& pTrack : pTracks) {
        m_pLoadedTracks.insert(pTrack.get()->getId(), pTrack);
    }

    updateFromTracks();

    // Listen to all tracks' changed() signal so we don't need to listen to
    // individual signals such as cuesUpdates, coverArtUpdated(), etc.
    connectTracksChanged();
}

void DlgTrackInfoMulti::updateFromTracks() {
    const QSignalBlocker signalBlocker(this);

    QList<mixxx::TrackRecord> trackRecords;
    trackRecords.reserve(m_pLoadedTracks.size());
    for (const auto& pTrack : std::as_const(m_pLoadedTracks)) {
        const auto rec = pTrack.get()->getRecord();
        trackRecords.append(rec);
    }
    replaceTrackRecords(trackRecords);

    // Collect star ratings and track colors
    // If track value differs from the current value, add it to the list.
    // If new and current are identical, keep only one.
    int commonRating = m_trackRecords.first().getRating();
    for (const auto& rec : m_trackRecords) {
        if (commonRating != rec.getRating()) {
            commonRating = 0;
            break;
        }
    }
    // Update the star widget
    // Block signals to not set the 'modified' flag.
    m_pWStarRating->blockSignals(true);
    m_pWStarRating->slotSetRating(commonRating);
    m_starRatingModified = false;
    m_pWStarRating->blockSignals(false);

    // Same procedure for the track color
    mixxx::RgbColor::optional_t commonColor = m_trackRecords.first().getColor();
    bool multipleColors = false;
    for (const auto& rec : m_trackRecords) {
        if (commonColor != rec.getColor()) {
            commonColor = mixxx::RgbColor::nullopt();
            multipleColors = true;
            break;
        }
    }
    // Paint the color selector and check the respective color picker button.
    // Paints a rainbow gardient in case of multiple colors.
    trackColorDialogSetColorStyleButton(commonColor, multipleColors);
    m_colorChanged = false;
    m_newColor = mixxx::RgbColor::nullopt();

    // And the track directory
    QSet<QString> dirs;
    QString firstDir = m_pLoadedTracks.constBegin().value()->getFileInfo().canonicalLocationPath();
    dirs.insert(firstDir);
    for (const auto& pTrack : std::as_const(m_pLoadedTracks)) {
        const auto dir = pTrack->getFileInfo().canonicalLocationPath();
        if (dir != firstDir) {
            dirs.insert(dir);
            break;
        }
    }
    setCommonValueOrVariousStringAndFormatFont(txtLocation, dirs);

    // And the cover label
    updateCoverArtFromTracks();
}

void DlgTrackInfoMulti::replaceTrackRecords(const QList<mixxx::TrackRecord>& trackRecords) {
    // Signals are already blocked
    m_trackRecords = std::move(trackRecords);

    updateTrackMetadataFields();
}

void DlgTrackInfoMulti::updateTrackMetadataFields() {
    // Editable fields
    QSet<QString> titles;
    QSet<QString> artists;
    QSet<QString> aTitles;
    QSet<QString> aArtists;
    QSet<QString> genres;
    QSet<QString> composers;
    QSet<QString> grouping;
    QSet<QString> years;
    QSet<QString> keys;
    QSet<QString> nums;
    QSet<QString> comments;
    QSet<double> bpms;
    QSet<uint32_t> bitrates;
    QSet<double> durations;
    QSet<uint32_t> samplerates;
    QSet<QString> filetypes;

    for (const auto& rec : m_trackRecords) {
        addToTagSet(&titles, rec.getMetadata().getTrackInfo().getTitle());
        addToTagSet(&artists, rec.getMetadata().getTrackInfo().getArtist());
        addToTagSet(&aTitles, rec.getMetadata().getAlbumInfo().getTitle());
        addToTagSet(&aArtists, rec.getMetadata().getAlbumInfo().getArtist());
        addToTagSet(&genres, rec.getMetadata().getTrackInfo().getGenre());
        addToTagSet(&composers, rec.getMetadata().getTrackInfo().getComposer());
        addToTagSet(&grouping, rec.getMetadata().getTrackInfo().getGrouping());
        addToTagSet(&years, rec.getMetadata().getTrackInfo().getYear());
        addToTagSet(&keys, rec.getMetadata().getTrackInfo().getKeyText());
        addToTagSet(&nums, rec.getMetadata().getTrackInfo().getTrackNumber());
        addToTagSet(&comments, rec.getMetadata().getTrackInfo().getComment());

        auto bpm = rec.getMetadata().getTrackInfo().getBpm();
        addToTagSet(&bpms, bpm.isValid() ? bpm.value() : mixxx::Bpm::kValueMin);

        auto bitrate = rec.getMetadata().getStreamInfo().getBitrate();
        addToTagSet(&bitrates, bitrate.isValid() ? bitrate.value() : 0);

        addToTagSet(&durations, rec.getMetadata().getDurationSecondsRounded());

        auto samplerate = rec.getMetadata().getStreamInfo().getSignalInfo().getSampleRate();
        addToTagSet(&samplerates, samplerate.isValid() ? samplerate.value() : 0);

        addToTagSet(&filetypes, rec.getFileType());
    }

    addValuesToComboBox(txtTitle, titles);
    addValuesToComboBox(txtArtist, artists);
    addValuesToComboBox(txtAlbum, aTitles);
    addValuesToComboBox(txtAlbumArtist, aArtists);
    addValuesToComboBox(txtGenre, genres);
    addValuesToComboBox(txtComposer, composers);
    addValuesToComboBox(txtGrouping, grouping);
    addValuesToComboBox(txtYear, years, true);
    // temporarily disable key validation
    txtKey->blockSignals(true);
    addValuesToComboBox(txtKey, keys, true);
    txtKey->blockSignals(false);
    addValuesToComboBox(txtTrackNumber, nums, true);

    // The comment tag is special: it's the only one that may have multiple lines,
    // but we can't have a multi-line editor and a combobox at the same time.
    // TODO(ronso0) Maybe we can, but for now we display all comments in the editor,
    // separated by dashed lines.
    addValuesToCommentBox(comments);

    // Non-editable fields: BPM, bitrate, samplerate, type and directory
    // For BPM, bitrate and samplerate we show a span if we have multiple values.
    if (bpms.size() > 1) {
        QList<double> bpmList = bpms.values();
        std::sort(bpmList.begin(), bpmList.end());
        txtBpm->setText(QString("%1").arg(bpmList.first(), 3, 'f', 1) +
                QChar('-') +
                QString("%1").arg(bpmList.last(), 3, 'f', 1));
    } else { // we have at least one value, might be invalid (0)
        double bpm = *bpms.constBegin();
        if (bpm == mixxx::Bpm::kValueMin) {
            txtBpm->clear();
        } else {
            txtBpm->setText(QString::number(bpm));
        }
    }

    QString bitrate;
    if (bitrates.size() > 1) {
        QList<uint32_t> brList = bitrates.values();
        std::sort(brList.begin(), brList.end());
        bitrate = QString::number(brList.first()) +
                QChar('-') +
                QString::number(brList.last());
    } else { // we have at least one value, though 0 is not necessarily invalid
        bitrate = QString::number(*bitrates.constBegin());
    }
    txtBitrate->setText(bitrate + QChar(' ') + mixxx::audio::Bitrate::unit());

    setCommonValueOrVariousStringAndFormatFont(txtSamplerate,
            samplerates,
            true, // bold if common value
            QStringLiteral("Hz"));

    setCommonValueOrVariousStringAndFormatFont(txtType, filetypes, true);

    if (durations.size() > 1) {
        QList<double> durList = durations.values();
        std::sort(durList.begin(), durList.end());
        txtDuration->setText(mixxx::Duration::formatTime(durList.first()) +
                QChar('-') +
                mixxx::Duration::formatTime(durList.last()));
    } else {
        txtDuration->setText(mixxx::Duration::formatTime(*durations.constBegin()));
    }
}

template<typename T>
void DlgTrackInfoMulti::addValuesToComboBox(QComboBox* pBox, QSet<T>& values, bool sort) {
    // Verify that T can be used for pBox->addItem()
    DEBUG_ASSERT(isOrCanConvertToQString(*values.constBegin()));

    pBox->clear();
    pBox->lineEdit()->setPlaceholderText(QString());

    VERIFY_OR_DEBUG_ASSERT(!values.isEmpty()) {
        pBox->setProperty(kOrigValProp, QString());
        return;
    }

    if (values.size() == 1) {
        pBox->setCurrentText(*values.constBegin());
        pBox->setProperty(kOrigValProp, *values.constBegin());
    } else {
        // The empty item allows to clear the text for all tracks.
        pBox->addItem(tr("clear tag for all tracks"), kClearItem);
        pBox->addItems(values.values());
        if (sort) {
            pBox->model()->sort(0);
        }
        pBox->setCurrentIndex(-1);
        // Show '<various>' placeholder.
        // The QComboBox::lineEdit() placeholder actually providex a nice UX:
        // it's displayed with a dim color and it persists until new text is
        // entered. However, this prevents clearing the text.
        pBox->lineEdit()->setPlaceholderText(kVariousText);
        pBox->setProperty(kOrigValProp, kVariousText);
    }
}

void DlgTrackInfoMulti::addValuesToCommentBox(QSet<QString>& comments) {
    txtComment->clear();
    txtCommentBox->clear();
    txtComment->setPlaceholderText(QString());

    VERIFY_OR_DEBUG_ASSERT(!comments.isEmpty()) {
        txtCommentBox->setProperty(kOrigValProp, QString());
        return;
    }

    txtCommentBox->blockSignals(true);
    if (comments.size() == 1) {
        txtCommentBox->setEnabled(false);
        txtComment->setPlainText(*comments.constBegin());
        txtComment->setProperty(kOrigValProp, *comments.constBegin());
    } else {
        txtCommentBox->setEnabled(true);
        // The empty item allows to clear the text for all tracks.
        txtCommentBox->addItem(tr("clear tag for all tracks"), kClearItem);
        txtCommentBox->addItems(comments.values());
        txtCommentBox->setCurrentIndex(-1);
        txtComment->setPlaceholderText(kVariousText);
        txtComment->setProperty(kOrigValProp, kVariousText);
    }
    txtCommentBox->blockSignals(false);
}

void DlgTrackInfoMulti::saveTracks() {
    if (m_pLoadedTracks.isEmpty()) {
        return;
    }

    // Check the values so we don't have to do it for every track record
    const QString title = validEditText(txtTitle);
    const QString artist = validEditText(txtArtist);
    const QString album = validEditText(txtAlbum);
    const QString albumArtist = validEditText(txtAlbumArtist);
    const QString genre = validEditText(txtGenre);
    const QString composer = validEditText(txtComposer);
    const QString grouping = validEditText(txtGrouping);
    const QString year = validEditText(txtYear);
    // In case Apply is triggered by hotkey AND a Key box with pending changes
    // is focused AND the user did not hit Enter to finish editing, the key text
    // needs to be validated.
    // This hack makes a focused txtKey's QLineEdit emits editingFinished()
    // (clearFocus() implicitly emits a focusOutEvent()).
    if (txtKey->hasFocus()) {
        txtKey->clearFocus();
        txtKey->setFocus();
    }
    const QString key = validEditText(txtKey);
    const QString num = validEditText(txtTrackNumber);

    QString comment;
    const QString origVal = txtComment->property(kOrigValProp).toString();
    const QString currVal = txtComment->toPlainText();
    if (txtComment->placeholderText().isNull() && currVal != origVal) {
        // This is either a single-value box and the value changed, or this is a
        // multi-value box and the placeholder text was removed when clearing it.
        comment = currVal.trimmed();
    }

    for (auto& rec : m_trackRecords) {
        if (!title.isNull()) {
            rec.refMetadata().refTrackInfo().setTitle(title);
        }
        if (!artist.isNull()) {
            rec.refMetadata().refTrackInfo().setArtist(artist);
        }
        if (!album.isNull()) {
            rec.refMetadata().refAlbumInfo().setTitle(album);
        }
        if (!albumArtist.isNull()) {
            rec.refMetadata().refAlbumInfo().setArtist(albumArtist);
        }
        if (!genre.isNull()) {
            rec.refMetadata().refTrackInfo().setGenre(genre);
        }
        if (!composer.isNull()) {
            rec.refMetadata().refTrackInfo().setComposer(composer);
        }
        if (!grouping.isNull()) {
            rec.refMetadata().refTrackInfo().setGrouping(grouping);
        }
        if (!year.isNull()) {
            rec.refMetadata().refTrackInfo().setYear(year);
        }
        if (!key.isNull()) {
            static_cast<void>(rec.updateGlobalKeyNormalizeText(
                    key,
                    mixxx::track::io::key::USER));
        }
        if (!num.isNull()) {
            rec.refMetadata().refTrackInfo().setTrackNumber(num);
        }
        if (!comment.isNull()) {
            rec.refMetadata().refTrackInfo().setComment(comment);
        }
        if (m_colorChanged) {
            rec.setColor(m_newColor);
        }
        if (m_starRatingModified) {
            rec.setRating(m_newRating);
        }
    }

    // First, disconnect the track changed signal. Otherwise we signal ourselves
    // and repopulate all these fields.
    disconnectTracksChanged();
    // Update the cached tracks
    for (const auto& rec : m_trackRecords) {
        auto pTrack = m_pLoadedTracks.value(rec.getId());
        // If replaceRecord() returns true then both m_trackRecord and m_pBeatsClone
        // will be updated by the subsequent Track::changed() signal to keep them
        // synchronized with the track. Otherwise the track has not been modified and
        // both members must remain valid. Do not use std::move() for passing arguments!
        // See https://github.com/mixxxdj/mixxx/issues/12963
        pTrack->replaceRecord(rec);
    }

    connectTracksChanged();

    // Repopulate the dialog and update the UI
    updateFromTracks();
}

void DlgTrackInfoMulti::clear() {
    const QSignalBlocker signalBlocker(this);

    disconnectTracksChanged();
    m_pLoadedTracks.clear();
    m_trackRecords.clear();

    m_pWStarRating->slotSetRating(0);
    trackColorDialogSetColorStyleButton(mixxx::RgbColor::nullopt());
    m_pWCoverArtLabel->loadTrack(TrackPointer());
    m_pWCoverArtLabel->setCoverArt(CoverInfo(), QPixmap());
}

void DlgTrackInfoMulti::connectTracksChanged() {
    for (const auto& pTrack : std::as_const(m_pLoadedTracks)) {
        connect(pTrack.get(),
                &Track::changed,
                this,
                &DlgTrackInfoMulti::slotTrackChanged);
    }
}

void DlgTrackInfoMulti::disconnectTracksChanged() {
    for (const auto& pTrack : std::as_const(m_pLoadedTracks)) {
        disconnect(pTrack.get(),
                &Track::changed,
                this,
                &DlgTrackInfoMulti::slotTrackChanged);
    }
}

void DlgTrackInfoMulti::slotImportMetadataFromFiles() {
    if (m_pLoadedTracks.isEmpty()) {
        return;
    }
    // Initialize the metadata with the current metadata to avoid
    // losing existing metadata or to lose the beat grid by replacing
    // it with a default grid created from an imprecise BPM.
    // See also: https://github.com/mixxxdj/mixxx/issues/10420
    // In addition we need to preserve all other track properties
    // that are stored in TrackRecord, which serves as the underlying
    // model for this dialog.
    QList<mixxx::TrackRecord> trackRecords;
    trackRecords.reserve(m_pLoadedTracks.size());
    for (const auto& pTrack : std::as_const(m_pLoadedTracks)) {
        auto trackRecord = pTrack->getRecord();
        auto trackMetadata = trackRecord.getMetadata();
        QImage coverImage;
        const auto resetMissingTagMetadata = m_pUserSettings->getValue<bool>(
                mixxx::library::prefs::kResetMissingTagMetadataOnImportConfigKey);
        const auto [importResult, sourceSynchronizedAt] =
                SoundSourceProxy(pTrack)
                        .importTrackMetadataAndCoverImage(
                                &trackMetadata, &coverImage, resetMissingTagMetadata);
        if (importResult != mixxx::MetadataSource::ImportResult::Succeeded) {
            // One track failed, abort. User feedback would be good.
            qWarning() << "DlgTrackInfoMulti::slotImportMetadataFromFiles: "
                          "failed to load metadata from file for track"
                       << pTrack->getId() << pTrack->getLocation();
            return;
        }
        auto guessedCoverInfo = CoverInfoGuesser().guessCoverInfo(
                pTrack->getFileInfo(),
                trackMetadata.getAlbumInfo().getTitle(),
                coverImage);
        trackRecord.replaceMetadataFromSource(
                std::move(trackMetadata),
                sourceSynchronizedAt);
        trackRecord.setCoverInfo(
                std::move(guessedCoverInfo));
        trackRecords.append(trackRecord);
    }
    replaceTrackRecords(trackRecords);
}

void DlgTrackInfoMulti::slotTrackChanged(TrackId trackId) {
    auto it = m_pLoadedTracks.constFind(trackId);
    if (it != m_pLoadedTracks.constEnd()) {
        updateFromTracks();
    }
}

void DlgTrackInfoMulti::slotKeyTextChanged() {
    QString newKeyText;
    mixxx::track::io::key::ChromaticKey newKey =
            KeyUtils::guessKeyFromText(txtKey->currentText().trimmed());
    if (newKey != mixxx::track::io::key::INVALID) {
        newKeyText = KeyUtils::keyToString(newKey);
    }

    txtKey->blockSignals(true);
    if (!newKeyText.isNull()) {
        txtKey->setCurrentText(newKeyText);
        txtKey->lineEdit()->setPlaceholderText(QString());
    } else {
        // Revert if we can't guess a valid key from it.
        if (txtKey->lineEdit()->placeholderText() == kVariousText) {
            // This is a multi-value box and the key has not been cleared manually.
            // Just clear the text to restore <various>.
            txtKey->clearEditText();
        } else {
            // This is a single-value box.Restore the original key text.
            const QString origKeyStr = txtKey->property(kOrigValProp).toString();
            txtKey->setCurrentText(origKeyStr);
        }
    }
    txtKey->blockSignals(false);
}

void DlgTrackInfoMulti::slotColorButtonClicked() {
    if (m_pLoadedTracks.isEmpty()) {
        return;
    }
    btnColorPicker->showMenu();
}

void DlgTrackInfoMulti::slotColorPicked(const mixxx::RgbColor::optional_t& newColor) {
    m_colorChanged = true;
    m_newColor = newColor;
    trackColorDialogSetColorStyleButton(newColor);
}

void DlgTrackInfoMulti::trackColorDialogSetColorStyleButton(
        const mixxx::RgbColor::optional_t& newColor,
        bool variousColors) {
    btnColorPicker->menu()->close();

    QString styleSheet;
    btnColorPicker->setText("");
    if (newColor) {
        const QColor ccolor = mixxx::RgbColor::toQColor(newColor);
        styleSheet = QStringLiteral(
                "QPushButton { background-color: %1; color: %2; }")
                             .arg(ccolor.name(QColor::HexRgb),
                                     Color::isDimColor(ccolor)
                                             ? "white"
                                             : "black");
        btnColorPicker->setText(ccolor.name(QColor::HexRgb));
        m_pColorPicker->setSelectedColor(newColor);
    } else if (variousColors) { // paint a horizontal rainbow gradient
        styleSheet = QStringLiteral(
                "QPushButton {"
                "background-color: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,"
                "           stop: 0 #FF0000,"
                "           stop: 0.2 #FFFF00,"
                "           stop: 0.4 #00FF00,"
                "           stop: 0.6 #00FFFF,"
                "           stop: 0.8 #0000FF,"
                "           stop: 1 #FF00FF)}");
        btnColorPicker->setText(kVariousText);
        m_pColorPicker->resetSelectedColor();
    } else { // no color
        btnColorPicker->setText(tr("(no color)"));
        m_pColorPicker->setSelectedColor(newColor);
    }
    btnColorPicker->setStyleSheet(styleSheet);
}

void DlgTrackInfoMulti::slotStarRatingChanged(int rating) {
    if (!m_pLoadedTracks.isEmpty() && mixxx::TrackRecord::isValidRating(rating)) {
        m_starRatingModified = true;
        m_pWStarRating->slotSetRating(rating);
        m_newRating = rating;
    }
}

void DlgTrackInfoMulti::updateCoverArtFromTracks() {
    VERIFY_OR_DEBUG_ASSERT(!m_pLoadedTracks.isEmpty()) {
        return;
    }
    CoverInfoRelative refCover = m_trackRecords.first().getCoverInfo();
    for (const auto& rec : m_trackRecords) {
        if (rec.getCoverInfo() != refCover) {
            refCover.reset();
            break;
        }
    }

    TrackPointer pRefTrack = m_pLoadedTracks.cbegin().value();
    // Regardless of cover match we load the reference track. That way,
    // the cover label has a track and location which is required to provide
    // the context menu and allow us to clear or change the cover.
    m_pWCoverArtLabel->loadTrack(pRefTrack);
    if (refCover.hasImage()) {
        // Covers are identical, we could load any track to the cover widget.
        // Just make sure the same track is used in slotCoverFound(): the track
        // location has to match in order to load the cover image to the label.
        auto trCover = pRefTrack->getCoverInfoWithLocation();
        m_pWCoverArtLabel->setCoverArt(trCover, QPixmap());
        CoverArtCache::requestCover(this, trCover);
    } else {
        // Set empty cover + track location
        auto trCover = CoverInfo(CoverInfoRelative(), pRefTrack->getLocation());
        m_pWCoverArtLabel->setCoverArt(trCover, QPixmap());
    }
}

void DlgTrackInfoMulti::slotCoverFound(
        const QObject* pRequester,
        const CoverInfo& coverInfo,
        const QPixmap& pixmap) {
    if (pRequester != this) {
        return;
    }
    VERIFY_OR_DEBUG_ASSERT(!m_pLoadedTracks.isEmpty()) {
        return;
    }
    // TODO Is this check really necessary? Is it possible that tracks
    // have changed while CoverArtCache was working on our request?
    if (m_pLoadedTracks.cbegin().value()->getLocation() == coverInfo.trackLocation) {
        // Track records have already been updated in slotCoverInfoSelected,
        // now load the image to the label.
        m_pWCoverArtLabel->setCoverArt(coverInfo, pixmap);
    }
}

void DlgTrackInfoMulti::slotCoverInfoSelected(const CoverInfoRelative& coverInfo) {
    VERIFY_OR_DEBUG_ASSERT(!m_pLoadedTracks.isEmpty()) {
        return;
    }
    for (auto& rec : m_trackRecords) {
        rec.setCoverInfo(coverInfo);
    }
    // Covers are now identical, we could load any track to cover widget.
    // Just make sure the same track is used in slotCoverFound(): the track
    // location has to match in order to load the cover image to the label.
    auto pFirstTrack = m_pLoadedTracks.constBegin().value();
    CoverArtCache::requestCover(this, CoverInfo(coverInfo, pFirstTrack->getLocation()));
}

void DlgTrackInfoMulti::slotReloadCoverArt() {
    for (auto& rec : m_trackRecords) {
        auto pTrack = getTrackFromSetById(rec.getId());
        if (pTrack == nullptr) {
            return;
        }
        auto cover = CoverInfoGuesser().guessCoverInfoForTrack(pTrack);
        rec.setCoverInfo(cover);
    }
    updateCoverArtFromTracks();
}
