#ifndef KOREAN_H
#define KOREAN_H

#ifdef __cplusplus
extern "C" {
#endif

const char* translate_and_map(const char* s);
void reset_korean_map();
uint16_t get_mapped_unicode(unsigned char c);

#ifdef __cplusplus
}
#endif

#endif
