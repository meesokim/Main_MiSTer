#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <string>
#include "korean.h"
#include "korean_data.h"

extern unsigned char charfont[256][8];

// Mapping structure for dynamic Hangul glyph slots
// We use slots 151 to 255 (105 slots total) to avoid conflicting with box/arrow symbols
#define KOREAN_START_SLOT 151
#define KOREAN_MAX_SLOTS  105

static uint16_t unicode_to_index[KOREAN_MAX_SLOTS];
static int mapped_count = 0;
static int next_slot = 0;

static const int jung_to_lookup_idx[21] = {
	3,  4,  5,  6,  7,  10, 11, 12, 13, 14,
	15, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29
};

static const int jong_to_lookup_idx[28] = {
	0,  // none
	2,  3,  4,  5,  6,  7,  8,  9,  10,
	11, 12, 13, 14, 15, 16, 17, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29
};

// Generates 8x8 Hangul bitmap dynamically on-the-fly from Jamo tables
void generate_hangul_bitmap(uint16_t unicode_char, unsigned char out_bitmap[8])
{
	memset(out_bitmap, 0, 8);
	if (unicode_char < 0xAC00 || unicode_char > 0xD7A3) return;

	uint16_t base = unicode_char - 0xAC00;
	int jong = base % 28;
	int jung = (base / 28) % 21;
	int cho  = (base / 28) / 21;

	int lookup_cho = cho + 2;
	int lookup_jung = jung_to_lookup_idx[jung];
	int lookup_jong = (jong > 0) ? jong_to_lookup_idx[jong] : 0;

	int idx = 0;
	if (jong > 0)
	{
		// Draw Coda (Jongseong)
		int coda_pos = lookup_jong * 8;
		for (int i = 0; i < 8; i++) out_bitmap[i] |= codaData[coda_pos + i];

		// Draw Nucleus (Jungseong) index 1
		int nucleus_pos = lookup_jung * 8;
		for (int i = 0; i < 8; i++) out_bitmap[i] |= nucleusData[1][nucleus_pos + i];

		// Select Choseong variant index based on Jungseong
		switch (jung)
		{
			case 0:  // ㅏ
			case 1:  // ㅐ
			case 2:  // ㅑ
			case 3:  // ㅒ
			case 4:  // ㅓ
			case 5:  // ㅔ
			case 6:  // ㅕ
			case 7:  // ㅖ
			case 20: // ㅣ
				idx = 3;
				break;
			case 8:  // ㅗ
			case 12: // ㅛ
			case 13: // ㅜ
			case 17: // ㅠ
			case 18: // ㅡ
				idx = 4;
				break;
			case 9:  // ㅘ
			case 10: // ㅙ
			case 11: // ㅚ
			case 14: // ㅝ
			case 15: // ㅞ
			case 16: // ㅟ
			case 19: // ㅢ
				idx = 5;
				break;
			default:
				break;
		}
	}
	else
	{
		// Draw Nucleus (Jungseong) index 0
		int nucleus_pos = lookup_jung * 8;
		for (int i = 0; i < 8; i++) out_bitmap[i] |= nucleusData[0][nucleus_pos + i];

		// Select Choseong variant index based on Jungseong
		switch (jung)
		{
			case 0:  // ㅏ
			case 1:  // ㅐ
			case 2:  // ㅑ
			case 3:  // ㅒ
			case 4:  // ㅓ
			case 5:  // ㅔ
			case 6:  // ㅕ
			case 7:  // ㅖ
			case 20: // ㅣ
				idx = 0;
				break;
			case 8:  // ㅗ
			case 12: // ㅛ
			case 13: // ㅜ
			case 17: // ㅠ
			case 18: // ㅡ
				idx = 1;
				break;
			case 9:  // ㅘ
			case 10: // ㅙ
			case 11: // ㅚ
			case 14: // ㅝ
			case 15: // ㅞ
			case 16: // ㅟ
			case 19: // ㅢ
				idx = 2;
				break;
			default:
				break;
		}
	}

	// Draw Onset (Choseong)
	int onset_pos = lookup_cho * 8;
	for (int i = 0; i < 8; i++) out_bitmap[i] |= onsetData[idx][onset_pos + i];
}

void reset_korean_map()
{
	mapped_count = 0;
	next_slot = 0;
	memset(unicode_to_index, 0, sizeof(unicode_to_index));
}

uint16_t get_mapped_unicode(unsigned char c)
{
	if (c >= KOREAN_START_SLOT && c < KOREAN_START_SLOT + mapped_count)
	{
		return unicode_to_index[c - KOREAN_START_SLOT];
	}
	return 0;
}

// Translates a UTF-8 Unicode point to a mapped slot index, updating the slot on-the-fly
static unsigned char get_hangul_slot(uint16_t u_char)
{
	// Search if already mapped
	for (int i = 0; i < mapped_count; i++)
	{
		if (unicode_to_index[i] == u_char)
		{
			return (unsigned char)(KOREAN_START_SLOT + i);
		}
	}

	// Not mapped yet. Allocate a slot.
	int slot_idx = 0;
	if (mapped_count < KOREAN_MAX_SLOTS)
	{
		slot_idx = mapped_count++;
	}
	else
	{
		// Wrap around / reuse oldest allocated slots
		slot_idx = next_slot;
		next_slot = (next_slot + 1) % KOREAN_MAX_SLOTS;
	}

	unicode_to_index[slot_idx] = u_char;
	
	// Generate the Hangul bitmap
	unsigned char bmp[8];
	generate_hangul_bitmap(u_char, bmp);

	// Write directly to the charfont table at this slot
	memcpy(charfont[KOREAN_START_SLOT + slot_idx], bmp, 8);

	return (unsigned char)(KOREAN_START_SLOT + slot_idx);
}

// Decodes a UTF-8 character from reference pointer and increments the pointer
static uint32_t decode_utf8(const char* &s)
{
	uint32_t c = (unsigned char)*s++;
	if (c < 0x80) return c;
	if ((c & 0xE0) == 0xC0)
	{
		uint32_t c2 = (unsigned char)*s++;
		return ((c & 0x1F) << 6) | (c2 & 0x3F);
	}
	if ((c & 0xF0) == 0xE0)
	{
		uint32_t c2 = (unsigned char)*s++;
		uint32_t c3 = (unsigned char)*s++;
		return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
	}
	if ((c & 0xF8) == 0xF0)
	{
		uint32_t c2 = (unsigned char)*s++;
		uint32_t c3 = (unsigned char)*s++;
		uint32_t c4 = (unsigned char)*s++;
		return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
	}
	return c;
}

struct TranslationEntry {
	const char* english;
	const char* korean;
};

static const TranslationEntry translation_dict[] = {
	{ "System Settings", "시스템 설정" },
	{ "Reset settings?", "설정을 초기화할까요?" },
	{ "Reset Minimig?", "Minimig를 초기화할까요?" },
	{ "Reset settings", "설정 초기화" },
	{ "Save settings", "설정 저장" },
	{ "Lock OSD", "OSD 잠금" },
	{ "Button/Key remap", "버튼/키 매핑" },
	{ "Reset player assignment", "플레이어 할당 재설정" },
	{ "Video processing", "비디오 설정" },
	{ "DIP Switches", "DIP 스위치" },
	{ "Core", "코어" },
	{ "Help", "도움말" },
	{ "About", "정보" },
	{ "Reboot (hold \x16 cold reboot)", "재부팅 (길게 누르면 콜드)" },
	{ "Reboot", "재부팅" },
	{ "Load preset", "프리셋 불러오기" },
	{ "Saving...", "저장 중..." },
	{ "Loading...", "불러오는 중..." },
	{ "Select", "선택" },
	{ "back", "뒤로" },
	{ "Back", "뒤로" },
	{ "exit", "종료" },
	{ "Exit", "종료" },
	{ "Yes", "예" },
	{ "No", "아니오" },
	{ "OK", "확인" },
	{ "Cancel", "취소" },
	{ "Error", "오류" },
	{ "Success", "성공" },
	{ "Warning", "경고" },
	{ "Information", "정보" },
	{ "No files!", "파일 없음!" },
	{ "Scanlines:  ", "스캔라인:  " },
	{ "Scale:  ", "배율:  " },
	{ "Filter:  ", "필터:  " },
	{ "Audio filter:  ", "오디오 필터:  " },
	{ "Aspect ratio:  ", "화면비:  " },
	{ "Custom aspect:  ", "사용자 지정 화면비:  " },
	{ "Custom aspect", "사용자 화면비" },
	{ "Palette:  ", "팔레트:  " },
	{ "Scanlines", "스캔라인" },
	{ "Audio filter", "오디오 필터" },
	{ "Reset", "재설정" },
	{ "Save", "저장" },
	{ "Load", "로드" },
	{ "Delete", "삭제" },
	{ "Rename", "이름 변경" },
	{ "Create Folder", "폴더 생성" },
	{ "Create folder", "폴더 생성" },
	{ "File Manager", "파일 관리자" },
	{ "Options", "옵션" },
	{ "Video", "비디오" },
	{ "Audio", "오디오" },
	{ "Cold reboot", "콜드 재부팅" }
};
#include <vector>
#include <algorithm>

static const int dict_size = sizeof(translation_dict) / sizeof(translation_dict[0]);

static std::vector<TranslationEntry> sorted_dict;
static bool dict_initialized = false;

static void initialize_dictionary()
{
	if (dict_initialized) return;
	for (int i = 0; i < dict_size; i++)
	{
		sorted_dict.push_back(translation_dict[i]);
	}
	std::sort(sorted_dict.begin(), sorted_dict.end(), [](const TranslationEntry& a, const TranslationEntry& b) {
		return strlen(a.english) > strlen(b.english);
	});
	dict_initialized = true;
}

// Translates prefix if a match is found
static std::string translate_prefix(const std::string& input)
{
	size_t lead_spaces = 0;
	while (lead_spaces < input.length() && input[lead_spaces] == ' ')
	{
		lead_spaces++;
	}
	std::string stripped = input.substr(lead_spaces);

	initialize_dictionary();
	for (const auto& entry : sorted_dict)
	{
		std::string en = entry.english;
		std::string ko = entry.korean;

		if (stripped.compare(0, en.length(), en) == 0)
		{
			std::string remaining = stripped.substr(en.length());
			return std::string(lead_spaces, ' ') + ko + remaining;
		}
	}
	return input;
}

const char* translate_and_map(const char* s)
{
	if (!s) return "";

	// Static circular buffer
	static char out_bufs[4][512];
	static int out_buf_idx = 0;

	char* out = out_bufs[out_buf_idx];
	out_buf_idx = (out_buf_idx + 1) % 4;

	// First, translate the string if it's English
	std::string translated = translate_prefix(s);

	// Map any UTF-8 Korean character to dynamic single-byte index slots
	const char* src = translated.c_str();
	char* dest = out;
	char* dest_end = out + 511;

	while (*src && dest < dest_end)
	{
		const char* prev_src = src;
		uint32_t u_char = decode_utf8(src);

		if (u_char >= 0xAC00 && u_char <= 0xD7A3)
		{
			// Map Korean character to single-byte slot index
			unsigned char slot = get_hangul_slot(u_char);
			*dest++ = slot;
		}
		else
		{
			// Copy character as-is (might be 1 or more bytes, but decode_utf8 consumes the bytes)
			// For simplicity and compatibility, copy the raw byte sequence of this code point
			int bytes_to_copy = src - prev_src;
			for (int b = 0; b < bytes_to_copy && dest < dest_end; b++)
			{
				*dest++ = prev_src[b];
			}
		}
	}
	*dest = '\0';

	return out;
}
