#include "music/track-tags.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void check(const char *what, const std::string &got, const std::string &want)
{
	if (got != want) {
		printf("FAIL %-28s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
		failures++;
	} else {
		printf("ok   %-28s '%s'\n", what, got.c_str());
	}
}

static void write_file(const char *path, const std::vector<uint8_t> &data)
{
	FILE *f = fopen(path, "wb");
	fwrite(data.data(), 1, data.size(), f);
	fclose(f);
}

static void put(std::vector<uint8_t> &v, const char *s, size_t n)
{
	v.insert(v.end(), s, s + n);
}
static void put_be32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back(x >> 24);
	v.push_back(x >> 16);
	v.push_back(x >> 8);
	v.push_back(x);
}
static void put_le32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back(x);
	v.push_back(x >> 8);
	v.push_back(x >> 16);
	v.push_back(x >> 24);
}
static void put_syncsafe(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back((x >> 21) & 0x7F);
	v.push_back((x >> 14) & 0x7F);
	v.push_back((x >> 7) & 0x7F);
	v.push_back(x & 0x7F);
}

/* --- ID3v2.3 --------------------------------------------------------- */
static void test_id3v23()
{
	std::vector<uint8_t> frames;
	auto frame = [&](const char *id, uint8_t enc, const std::string &text) {
		std::vector<uint8_t> body;
		body.push_back(enc);
		body.insert(body.end(), text.begin(), text.end());
		put(frames, id, 4);
		put_be32(frames, (uint32_t)body.size());
		frames.push_back(0);
		frames.push_back(0);
		frames.insert(frames.end(), body.begin(), body.end());
	};

	frame("TIT2", 3, "Neon Drift");
	frame("TPE1", 3, "Voidscape");
	frame("TALB", 3, "After Hours");
	frame("TYER", 0, "2024");
	frame("TRCK", 0, "3/12");
	frame("TCON", 0, "(52)");

	std::vector<uint8_t> file;
	put(file, "ID3", 3);
	file.push_back(3);
	file.push_back(0);
	file.push_back(0);
	put_syncsafe(file, (uint32_t)frames.size());
	file.insert(file.end(), frames.begin(), frames.end());
	file.resize(file.size() + 64, 0);

	write_file("/tmp/vr-test-id3v23.mp3", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test-id3v23.mp3", tags);
	check("id3v2.3 title", tags.title, "Neon Drift");
	check("id3v2.3 artist", tags.artist, "Voidscape");
	check("id3v2.3 album", tags.album, "After Hours");
	check("id3v2.3 year", tags.year, "2024");
	check("id3v2.3 track", tags.track, "3");
	check("id3v2.3 genre", tags.genre, "Electronic");
}

/* --- ID3v2.4 with UTF-16 --------------------------------------------- */
static void test_id3v24_utf16()
{
	std::vector<uint8_t> frames;
	std::vector<uint8_t> body;
	body.push_back(1);
	body.push_back(0xFF);
	body.push_back(0xFE);
	const char16_t text[] = u"Ünïcode Søñg";
	for (size_t i = 0; text[i]; i++) {
		body.push_back(text[i] & 0xFF);
		body.push_back(text[i] >> 8);
	}
	put(frames, "TIT2", 4);
	put_syncsafe(frames, (uint32_t)body.size());
	frames.push_back(0);
	frames.push_back(0);
	frames.insert(frames.end(), body.begin(), body.end());

	std::vector<uint8_t> file;
	put(file, "ID3", 3);
	file.push_back(4);
	file.push_back(0);
	file.push_back(0);
	put_syncsafe(file, (uint32_t)frames.size());
	file.insert(file.end(), frames.begin(), frames.end());

	write_file("/tmp/vr-test-id3v24.mp3", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test-id3v24.mp3", tags);
	check("id3v2.4 utf16 title", tags.title, "Ünïcode Søñg");
}

/* --- ID3v1 fallback --------------------------------------------------- */
static void test_id3v1()
{
	std::vector<uint8_t> file(4096, 0);
	std::vector<uint8_t> tag(128, 0);
	memcpy(&tag[0], "TAG", 3);
	memcpy(&tag[3], "Old School", 10);
	memcpy(&tag[33], "Tape Deck", 9);
	memcpy(&tag[93], "1998", 4);
	tag[125] = 0;
	tag[126] = 7;
	tag[127] = 17; /* Rock */
	file.insert(file.end(), tag.begin(), tag.end());

	write_file("/tmp/vr-test-id3v1.mp3", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test-id3v1.mp3", tags);
	check("id3v1 title", tags.title, "Old School");
	check("id3v1 artist", tags.artist, "Tape Deck");
	check("id3v1 year", tags.year, "1998");
	check("id3v1 track", tags.track, "7");
	check("id3v1 genre", tags.genre, "Rock");
}

/* --- FLAC ------------------------------------------------------------- */
static std::vector<uint8_t> vorbis_comment_block(const std::vector<std::string> &comments)
{
	std::vector<uint8_t> block;
	const std::string vendor = "void-radio-test";
	put_le32(block, (uint32_t)vendor.size());
	block.insert(block.end(), vendor.begin(), vendor.end());
	put_le32(block, (uint32_t)comments.size());
	for (const std::string &c : comments) {
		put_le32(block, (uint32_t)c.size());
		block.insert(block.end(), c.begin(), c.end());
	}
	return block;
}

static void test_flac()
{
	std::vector<uint8_t> comment =
		vorbis_comment_block({"TITLE=Deep Field", "ARTIST=Nebula Bloom", "ALBUM=Parallax", "DATE=2021-04-05",
				      "TRACKNUMBER=2/9", "GENRE=Ambient"});
	std::vector<uint8_t> file;
	put(file, "fLaC", 4);
	/* STREAMINFO block first, as a real file has. */
	file.push_back(0);
	file.push_back(0);
	file.push_back(0);
	file.push_back(34);
	file.resize(file.size() + 34, 0);
	file.push_back(0x80 | 4);
	file.push_back((comment.size() >> 16) & 0xFF);
	file.push_back((comment.size() >> 8) & 0xFF);
	file.push_back(comment.size() & 0xFF);
	file.insert(file.end(), comment.begin(), comment.end());

	write_file("/tmp/vr-test.flac", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test.flac", tags);
	check("flac title", tags.title, "Deep Field");
	check("flac artist", tags.artist, "Nebula Bloom");
	check("flac album", tags.album, "Parallax");
	check("flac year", tags.year, "2021");
	check("flac track", tags.track, "2");
	check("flac genre", tags.genre, "Ambient");
}

/* --- Ogg Vorbis, comment packet split across two pages ---------------- */
static void put_ogg_page(std::vector<uint8_t> &file, const std::vector<uint8_t> &packet_part, bool continued,
			 uint32_t seq)
{
	std::vector<uint8_t> segments;
	size_t remaining = packet_part.size();
	while (remaining >= 255) {
		segments.push_back(255);
		remaining -= 255;
	}
	segments.push_back((uint8_t)remaining);

	put(file, "OggS", 4);
	file.push_back(0);
	file.push_back(continued ? 0x01 : 0x00);
	for (int i = 0; i < 8; i++)
		file.push_back(0); /* granule */
	put_le32(file, 1);         /* serial */
	put_le32(file, seq);
	put_le32(file, 0); /* crc, unchecked */
	file.push_back((uint8_t)segments.size());
	file.insert(file.end(), segments.begin(), segments.end());
	file.insert(file.end(), packet_part.begin(), packet_part.end());
}

static void test_ogg_multipage()
{
	std::vector<uint8_t> ident;
	ident.push_back(1);
	put(ident, "vorbis", 6);
	ident.resize(30, 0);

	std::vector<uint8_t> comment;
	comment.push_back(3);
	put(comment, "vorbis", 6);
	/* A large padding comment forces the packet across a page boundary. */
	std::vector<uint8_t> body = vorbis_comment_block(
		{"TITLE=Split Packet", "ARTIST=Page Break", std::string("PADDING=") + std::string(600, 'x')});
	comment.insert(comment.end(), body.begin(), body.end());
	comment.push_back(1); /* framing bit */

	std::vector<uint8_t> file;
	put_ogg_page(file, ident, false, 0);

	const size_t split = 300;
	std::vector<uint8_t> first(comment.begin(), comment.begin() + split);
	std::vector<uint8_t> rest(comment.begin() + split, comment.end());

	/* First page ends mid-packet: pad it to a 255 byte lacing boundary. */
	std::vector<uint8_t> first255(comment.begin(), comment.begin() + 255);
	std::vector<uint8_t> remainder(comment.begin() + 255, comment.end());
	put_ogg_page(file, first255, false, 1);
	put_ogg_page(file, remainder, true, 2);

	write_file("/tmp/vr-test.ogg", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test.ogg", tags);
	check("ogg title (2 pages)", tags.title, "Split Packet");
	check("ogg artist (2 pages)", tags.artist, "Page Break");
	(void)first;
	(void)rest;
	(void)split;
}

/* --- Opus ------------------------------------------------------------- */
static void test_opus()
{
	std::vector<uint8_t> head;
	put(head, "OpusHead", 8);
	head.resize(19, 0);

	std::vector<uint8_t> tags_packet;
	put(tags_packet, "OpusTags", 8);
	std::vector<uint8_t> body = vorbis_comment_block({"TITLE=Opus Track", "ARTIST=Codec Crew"});
	tags_packet.insert(tags_packet.end(), body.begin(), body.end());

	std::vector<uint8_t> file;
	put_ogg_page(file, head, false, 0);
	put_ogg_page(file, tags_packet, false, 1);

	write_file("/tmp/vr-test.opus", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test.opus", tags);
	check("opus title", tags.title, "Opus Track");
	check("opus artist", tags.artist, "Codec Crew");
}

/* --- MP4 / M4A -------------------------------------------------------- */
static void append_box(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &content)
{
	put_be32(out, (uint32_t)(content.size() + 8));
	put(out, type, 4);
	out.insert(out.end(), content.begin(), content.end());
}

static std::vector<uint8_t> ilst_text(const char *type, const std::string &value)
{
	std::vector<uint8_t> data;
	put_be32(data, 1); /* version + flags: UTF-8 */
	put_be32(data, 0); /* locale */
	data.insert(data.end(), value.begin(), value.end());

	std::vector<uint8_t> data_box;
	append_box(data_box, "data", data);

	std::vector<uint8_t> item;
	append_box(item, type, data_box);
	return item;
}

static void test_mp4()
{
	std::vector<uint8_t> ilst;
	for (auto &item : {ilst_text("\xa9"
				     "nam",
				     "Glass Tower"),
			   ilst_text("\xa9"
				     "ART",
				     "Skyline"),
			   ilst_text("\xa9"
				     "alb",
				     "Vertigo"),
			   ilst_text("\xa9"
				     "day",
				     "2019-11-01"),
			   ilst_text("\xa9"
				     "gen",
				     "Downtempo")})
		ilst.insert(ilst.end(), item.begin(), item.end());

	std::vector<uint8_t> trkn_data;
	put_be32(trkn_data, 0);
	put_be32(trkn_data, 0);
	trkn_data.push_back(0);
	trkn_data.push_back(0);
	trkn_data.push_back(0);
	trkn_data.push_back(5);
	trkn_data.push_back(0);
	trkn_data.push_back(11);
	std::vector<uint8_t> trkn_databox;
	append_box(trkn_databox, "data", trkn_data);
	append_box(ilst, "trkn", trkn_databox);

	std::vector<uint8_t> meta;
	put_be32(meta, 0); /* full box version + flags */
	append_box(meta, "ilst", ilst);

	std::vector<uint8_t> udta;
	append_box(udta, "meta", meta);

	std::vector<uint8_t> moov;
	append_box(moov, "udta", udta);

	std::vector<uint8_t> file;
	std::vector<uint8_t> ftyp_content;
	put(ftyp_content, "M4A ", 4);
	put_be32(ftyp_content, 0);
	append_box(file, "ftyp", ftyp_content);
	append_box(file, "mdat", std::vector<uint8_t>(128, 0));
	append_box(file, "moov", moov);

	write_file("/tmp/vr-test.m4a", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test.m4a", tags);
	check("mp4 title", tags.title, "Glass Tower");
	check("mp4 artist", tags.artist, "Skyline");
	check("mp4 album", tags.album, "Vertigo");
	check("mp4 year", tags.year, "2019");
	check("mp4 genre", tags.genre, "Downtempo");
	check("mp4 track", tags.track, "5");
}

/* --- WAV RIFF INFO ---------------------------------------------------- */
static void test_wav()
{
	std::vector<uint8_t> info;
	put(info, "INFO", 4);
	auto add = [&](const char *id, const std::string &value) {
		std::string padded = value;
		padded.push_back('\0');
		if (padded.size() & 1)
			padded.push_back('\0');
		put(info, id, 4);
		put_le32(info, (uint32_t)padded.size());
		info.insert(info.end(), padded.begin(), padded.end());
	};
	add("INAM", "Wave Rider");
	add("IART", "Sampler");
	add("ICRD", "2005");

	std::vector<uint8_t> body;
	put(body, "WAVE", 4);
	put(body, "fmt ", 4);
	put_le32(body, 16);
	body.resize(body.size() + 16, 0);
	put(body, "LIST", 4);
	put_le32(body, (uint32_t)info.size());
	body.insert(body.end(), info.begin(), info.end());

	std::vector<uint8_t> file;
	put(file, "RIFF", 4);
	put_le32(file, (uint32_t)body.size());
	file.insert(file.end(), body.begin(), body.end());

	write_file("/tmp/vr-test.wav", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test.wav", tags);
	check("wav title", tags.title, "Wave Rider");
	check("wav artist", tags.artist, "Sampler");
	check("wav year", tags.year, "2005");
}

/* --- Garbage input must not crash or invent tags ---------------------- */
static void test_garbage()
{
	std::vector<uint8_t> file;
	put(file, "ID3", 3);
	file.push_back(3);
	file.push_back(0);
	file.push_back(0);
	put_syncsafe(file, 0xFFFFFF);
	for (int i = 0; i < 64; i++)
		file.push_back((uint8_t)(i * 37));
	write_file("/tmp/vr-test-garbage.mp3", file);

	vr::Tags tags;
	vr::read_tags("/tmp/vr-test-garbage.mp3", tags);
	check("garbage yields nothing", tags.title, "");

	vr::Tags empty;
	vr::read_tags("/tmp/vr-does-not-exist.mp3", empty);
	check("missing file", empty.title, "");
}

int main()
{
	test_id3v23();
	test_id3v24_utf16();
	test_id3v1();
	test_flac();
	test_ogg_multipage();
	test_opus();
	test_mp4();
	test_wav();
	test_garbage();

	printf("\n%s (%d failures)\n", failures ? "FAILURES" : "ALL TAG TESTS PASSED", failures);
	return failures ? 1 : 0;
}
