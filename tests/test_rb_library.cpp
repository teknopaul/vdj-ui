#include <QtTest>

#include <algorithm>
#include <set>

#include "dbserver/rb_library.h"

using namespace vdj::db;

namespace {

const QString kExport = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export");

int countCb(const pdb_track_t*, void* ud)
{
    ++*static_cast<int*>(ud);
    return 0;
}

int countNamedCb(const pdb_named_t*, void* ud)
{
    ++*static_cast<int*>(ud);
    return 0;
}

} // namespace

class TestRbLibrary : public QObject {
    Q_OBJECT

private:
    RbLibrary lib_;

private slots:
    void initTestCase()
    {
        std::string err;
        QVERIFY2(lib_.load(kExport.toStdString(), &err), err.c_str());
    }

    void loadFailsForMissingExport()
    {
        RbLibrary l;
        std::string err;
        QVERIFY(!l.load("/nonexistent/export", &err));
        QVERIFY(!err.empty());
        QVERIFY(!l.loaded());
    }

    void trailingSlashIsAccepted()
    {
        RbLibrary l;
        QVERIFY(l.load((kExport + "/").toStdString()));
        QCOMPARE(l.trackCount(), lib_.trackCount());
    }

    void countsMatchLibcdj()
    {
        uint32_t len = 0;
        const std::string pdb = (kExport + "/PIONEER/rekordbox/export.pdb").toStdString();
        uint8_t* buf = pdb_read_file(pdb.c_str(), &len);
        QVERIFY(buf);
        int tracks = 0, artists = 0, genres = 0;
        QCOMPARE(pdb_parse(buf, len, countCb, &tracks), 0);
        pdb_parse_artists(buf, len, countNamedCb, &artists);
        pdb_parse_genres(buf, len, countNamedCb, &genres);
        free(buf);
        QVERIFY(tracks > 100);
        QCOMPARE(int(lib_.trackCount()), tracks);
        QCOMPARE(int(lib_.artists().size()), artists);
        QCOMPARE(int(lib_.genres().size()), genres);
    }

    void trackFields()
    {
        const RbTrack* t = lib_.track(1935);
        QVERIFY(t);
        QCOMPARE(QString::fromStdString(t->title), QStringLiteral("Baby It's Me (Original Mix)"));
        QCOMPARE(t->analyzePath, std::string("/PIONEER/USBANLZ/P042/00015D16/ANLZ0000.DAT"));
        QVERIFY(t->durationSecs > 0);
        QVERIFY(!lib_.artistName(t->artistId).empty());
        QVERIFY(!lib_.albumName(t->albumId).empty());
        QVERIFY(lib_.track(0xfffffff0) == nullptr);
    }

    void everySortIsAPermutationAndOrdered()
    {
        const size_t n = lib_.trackCount();
        for (int sort : {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x11, 0x77}) {
            const auto ids = lib_.allTracks(sort);
            QCOMPARE(ids.size(), n);
            QCOMPARE(std::set<uint32_t>(ids.begin(), ids.end()).size(), n);
            // the ordering must be stable between calls
            QVERIFY(ids == lib_.allTracks(sort));
        }
        auto titles = lib_.allTracks(SortTitle);
        for (size_t i = 1; i < titles.size(); ++i)
            QVERIFY(lowerAscii(lib_.track(titles[i - 1])->title) <= lowerAscii(lib_.track(titles[i])->title));
        auto bpm = lib_.allTracks(SortBpm);
        for (size_t i = 1; i < bpm.size(); ++i) QVERIFY(lib_.track(bpm[i - 1])->bpmX100 <= lib_.track(bpm[i])->bpmX100);
        auto time = lib_.allTracks(SortTime);
        for (size_t i = 1; i < time.size(); ++i) QVERIFY(lib_.track(time[i - 1])->durationSecs <= lib_.track(time[i])->durationSecs);
        auto artist = lib_.allTracks(SortArtist);
        for (size_t i = 1; i < artist.size(); ++i)
            QVERIFY(lowerAscii(lib_.artistName(lib_.track(artist[i - 1])->artistId)) <= lowerAscii(lib_.artistName(lib_.track(artist[i])->artistId)));
    }

    void filtersAreConsistent()
    {
        const RbTrack* t = lib_.track(1935);
        QVERIFY(t);

        const auto byArtist = lib_.tracksForArtist(t->artistId);
        QVERIFY(std::find(byArtist.begin(), byArtist.end(), 1935u) != byArtist.end());
        size_t expected = 0;
        for (uint32_t id : lib_.allTracks())
            if (lib_.track(id)->artistId == t->artistId) ++expected;
        QCOMPARE(byArtist.size(), expected);
        for (uint32_t id : byArtist) QCOMPARE(lib_.track(id)->artistId, t->artistId);

        const auto byAlbum = lib_.tracksForAlbum(t->albumId);
        for (uint32_t id : byAlbum) QCOMPARE(lib_.track(id)->albumId, t->albumId);
        QVERIFY(std::find(byAlbum.begin(), byAlbum.end(), 1935u) != byAlbum.end());

        const auto both = lib_.tracksForArtistAlbum(t->artistId, t->albumId);
        QVERIFY(both.size() <= byAlbum.size() && both.size() <= byArtist.size());
        QVERIFY(std::find(both.begin(), both.end(), 1935u) != both.end());

        const auto albums = lib_.albumsForArtist(t->artistId);
        bool found = false;
        for (const RbAlbum& a : albums) found = found || a.id == t->albumId;
        QVERIFY(found);

        if (t->genreId) {
            const auto artists = lib_.artistsForGenre(t->genreId);
            bool a = false;
            for (const RbNamed& n : artists) a = a || n.id == t->artistId;
            QVERIFY(a);
            QVERIFY(!lib_.albumsForGenreArtist(t->genreId, t->artistId).empty());
            const auto byGenre = lib_.tracksForGenre(t->genreId);
            QVERIFY(std::find(byGenre.begin(), byGenre.end(), 1935u) != byGenre.end());
        }
        QVERIFY(lib_.tracksForArtist(0xfffffff0).empty());
    }

    void playlistTree()
    {
        const auto root = lib_.playlistChildren(0);
        QVERIFY(!root.empty());
        for (size_t i = 1; i < root.size(); ++i) QVERIFY(root[i - 1].sortOrder <= root[i].sortOrder);

        // walk the tree: every playlist's tracks exist, every child's parent matches
        size_t tracks = 0, lists = 0;
        std::vector<uint32_t> todo;
        for (const auto& n : root) todo.push_back(n.id);
        std::vector<RbPlaylistNode> nodes = root;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].isFolder) {
                for (const auto& c : lib_.playlistChildren(nodes[i].id)) {
                    QCOMPARE(c.parentId, nodes[i].id);
                    nodes.push_back(c);
                }
            } else {
                ++lists;
                const auto t = lib_.playlistTracks(nodes[i].id);
                tracks += t.size();
                for (uint32_t id : t) QVERIFY(lib_.track(id));
            }
        }
        QVERIFY(lists > 0);
        QVERIFY(tracks > 0);
        QVERIFY(lib_.playlistTracks(0xfffffff0).empty());
    }

    void searchFindsKnownTitleCaseInsensitively()
    {
        const auto hits = lib_.searchTracks("BABY IT'S ME");
        QVERIFY(std::find(hits.begin(), hits.end(), 1935u) != hits.end());
        QVERIFY(lib_.searchTracks("zzzz-no-such-title-zzzz").empty());
        const RbTrack* t = lib_.track(1935);
        bool found = false;
        for (const RbNamed& a : lib_.searchArtists(lib_.artistName(t->artistId))) found = found || a.id == t->artistId;
        QVERIFY(found);
        found = false;
        for (const RbAlbum& a : lib_.searchAlbums(lib_.albumName(t->albumId))) found = found || a.id == t->albumId;
        QVERIFY(found);
    }

    void analysisOfAnalysedTrack()
    {
        auto a = lib_.analysis(1935);      // the anchor track: 716 beats, 123.00 BPM, one hot-cue loop
        QVERIFY(a);
        anlz_beatgrid_t g{};
        QVERIFY(a->beatGrid(&g));
        QCOMPARE(g.count, 716u);
        QVERIFY(std::abs(anlz_avg_bpm(&g) - 123.0) < 0.5);

        std::vector<anlz_cue_t> cues;
        QVERIFY(a->cues(ANLZ_CUES_HOT, &cues));
        QVERIFY(!cues.empty());
        QVERIFY(std::any_of(cues.begin(), cues.end(), [](const anlz_cue_t& c) { return c.is_loop; }));

        anlz_waveform_t w{};
        QVERIFY(a->waveform(ANLZ_WAVE_PREVIEW, &w));
        QCOMPARE(w.count, 400u);
        QVERIFY(a->file(ANLZ_FILE_DAT) != nullptr);
        QVERIFY(a->file(ANLZ_FILE_EXT) != nullptr);

        QVERIFY(lib_.analysis(1935) == a);          // cached
    }

    void analysisOfUnanalysedTrackIsEmptyNotAnError()
    {
        auto a = lib_.analysis(1372);               // rekordbox did not analyse this one
        QVERIFY(a);
        anlz_beatgrid_t g{};
        if (a->beatGrid(&g)) QCOMPARE(g.count, 0u);
    }

    void analysisMissingFilesGivesNull()
    {
        QVERIFY(lib_.analysis(2) == nullptr);       // in the pdb, files not in the fixture export
        QVERIFY(lib_.analysis(0xfffffff0) == nullptr);
    }

    void analysisCacheEvictsButStaysValid()
    {
        auto first = lib_.analysis(1935);
        QVERIFY(first);
        for (uint32_t id : {34u, 2096u, 1372u, 516u, 1586u, 1932u, 2u, 11u, 16u, 14u, 153u, 24u})
            lib_.analysis(id);
        anlz_beatgrid_t g{};
        QVERIFY(first->beatGrid(&g));               // still valid though evicted from the cache
        QCOMPARE(g.count, 716u);
    }

    void artworkBytes()
    {
        const RbTrack* t = lib_.track(34);
        QVERIFY(t);
        QCOMPARE(t->artworkId, 34u);
        const auto a = lib_.artwork(34);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(a.data()), int(a.size())), QByteArray("FAKE-ARTWORK-34\n"));
        QVERIFY(lib_.artwork(2).empty());           // row exists, file does not
        QVERIFY(lib_.artwork(0).empty());
        QVERIFY(lib_.artwork(0xfffffff0).empty());
    }
};

QTEST_APPLESS_MAIN(TestRbLibrary)
#include "test_rb_library.moc"
