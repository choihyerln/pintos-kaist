#include <string.h>
#include <debug.h>

/* SRC에서 DST로 SIZE 바이트를 복사합니다.
   SRC와 DST가 겹치면 안 됩니다.
   DST를 반환합니다. */
void *
memcpy (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	while (size-- > 0)
		*dst++ = *src++;

	return dst_;
}

/* SRC에서 DST로 SIZE 바이트를 복사합니다.
   SRC와 DST가 겹쳐도 상관없습니다.
   DST를 반환합니다. */
void *
memmove (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	if (dst < src) {
		while (size-- > 0)
			*dst++ = *src++;
	} else {
		dst += size;
		src += size;
		while (size-- > 0)
			*--dst = *--src;
	}

	return dst;
}

/* A와 B의 SIZE 바이트 블록에서 처음으로 다른 바이트를 찾습니다.
   A의 바이트가 더 큰 경우 양수를 반환하고, B의 바이트가 더 큰 경우 음수를 반환하며,
   A와 B가 동일한 경우에는 0을 반환합니다. */
int
memcmp (const void *a_, const void *b_, size_t size) {
	const unsigned char *a = a_;
	const unsigned char *b = b_;

	ASSERT (a != NULL || size == 0);
	ASSERT (b != NULL || size == 0);

	for (; size-- > 0; a++, b++)
		if (*a != *b)
			return *a > *b ? +1 : -1;
	return 0;
}

/* 문자열 A와 B에서 처음으로 다른 문자를 찾습니다.
   A의 문자가 더 큰 경우 양수를 반환하고, B의 문자가 더 큰 경우 음수를 반환하며,
   A와 B가 동일한 경우에는 0을 반환합니다. 
   (각 문자는 unsigned char로 취급됩니다.) */
int
strcmp (const char *a_, const char *b_) {
	const unsigned char *a = (const unsigned char *) a_;
	const unsigned char *b = (const unsigned char *) b_;

	ASSERT (a != NULL);
	ASSERT (b != NULL);

	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}

	return *a < *b ? -1 : *a > *b;
}

/* BLOCK에서 시작하는 SIZE 바이트 중에서 CH가 처음 나타나는 위치의 포인터를 반환합니다.
   CH가 BLOCK에 나타나지 않으면 널 포인터를 반환합니다. */
void *
memchr (const void *block_, int ch_, size_t size) {
	const unsigned char *block = block_;
	unsigned char ch = ch_;

	ASSERT (block != NULL || size == 0);

	for (; size-- > 0; block++)
		if (*block == ch)
			return (void *) block;

	return NULL;
}

/* 문자열 STRING에서 문자 C의 첫 번째 등장을 찾아 반환하거나,
   C가 STRING에 나타나지 않으면 널 포인터를 반환합니다.
   C가 '\0'인 경우, STRING 끝에 있는 널 종결자를 */
char *
strchr (const char *string, int c_) {
	char c = c_;

	ASSERT (string);

	for (;;)
		if (*string == c)
			return (char *) string;
		else if (*string == '\0')
			return NULL;
		else
			string++;
}

/* 문자열 STRING의 초기 부분 중, STOP에 포함되지 않는 문자로 구성된 부분 문자열의 길이를 반환합니다. */
size_t
strcspn (const char *string, const char *stop) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (stop, string[length]) != NULL)
			break;
	return length;
}

/* 문자열 STRING 중 STOP에 포함된 첫 번째 문자를 가리키는 포인터를 반환합니다.
   STRING에 STOP에 포함된 문자가 없으면 널 포인터를 반환합니다. */
char *
strpbrk (const char *string, const char *stop) {
	for (; *string != '\0'; string++)
		if (strchr (stop, *string) != NULL)
			return (char *) string;
	return NULL;
}

/* 문자열 STRING에서 문자 C의 마지막 등장을 가리키는 포인터를 반환합니다.
   C가 STRING에 나타나지 않으면 널 포인터를 반환합니다. */
char *
strrchr (const char *string, int c_) {
	char c = c_;
	const char *p = NULL;

	for (; *string != '\0'; string++)
		if (*string == c)
			p = string;
	return (char *) p;
}

/* 문자열 STRING의 초기 부분 중, SKIP에 포함된 문자로 구성된 부분 문자열의 길이를 반환합니다. */
size_t
strspn (const char *string, const char *skip) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (skip, string[length]) == NULL)
			break;
	return length;
}

/* HAYSTACK에서 NEEDLE의 첫 등장을 가리키는 포인터를 반환합니다.
   NEEDLE이 HAYSTACK에 존재하지 않으면 널 포인터를 반환합니다. */

char *
strstr (const char *haystack, const char *needle) {
	size_t haystack_len = strlen (haystack);
	size_t needle_len = strlen (needle);

	if (haystack_len >= needle_len) {
		size_t i;

		for (i = 0; i <= haystack_len - needle_len; i++)
			if (!memcmp (haystack + i, needle, needle_len))
				return (char *) haystack + i;
	}

	return NULL;
}

/* 문자열을 DELIMITERS로 구분된 토큰으로 분할합니다.
   이 함수를 처음 호출할 때 S는 토큰화할 문자열이어야 하며,
   이후의 호출에서는 널 포인터여야 합니다.
   SAVE_PTR는 토크나이저의 위치를 추적하는 데 사용되는 'char *' 변수의 주소입니다.
   각 호출마다 반환 값은 문자열의 다음 토큰이거나,
   토큰이 더 이상 없으면 널 포인터입니다.

   이 함수는 연속된 여러 구분자를 하나의 구분자로 처리합니다.
   반환된 토큰의 길이는 항상 0이 아니게 됩니다.
   DELIMITERS는 한 문자열 내에서 호출 사이에 바뀔 수 있습니다.

   strtok_r()은 문자열 S를 수정하여 구분자를 널 바이트로 변경합니다.
   따라서 S는 수정 가능한 문자열이어야 합니다.
   특히, 문자열 리터럴은 C에서 수정할 수 없지만,
   이에 대한 하위 호환성을 위해 `const`가 아닙니다.

   사용 예시:

   char s[] = "  String to  tokenize. ";
   char *token, *save_ptr;

   for (token = strtok_r(s, " ", &save_ptr); token != NULL;
        token = strtok_r(NULL, " ", &save_ptr))
        printf("'%s'\n", token);

출력:

'String'
'to'
'tokenize.'
*/
char *
strtok_r (char *s, const char *delimiters, char **save_ptr) {
	char *token;

	ASSERT (delimiters != NULL);
	ASSERT (save_ptr != NULL);

	/* If S is nonnull, start from it.
	   If S is null, start from saved position. */
	if (s == NULL)
		s = *save_ptr;
	ASSERT (s != NULL);

	/* Skip any DELIMITERS at our current position. */
	while (strchr (delimiters, *s) != NULL) {
		/* strchr() will always return nonnull if we're searching
		   for a null byte, because every string contains a null
		   byte (at the end). */
		if (*s == '\0') {
			*save_ptr = s;
			return NULL;
		}

		s++;
	}

	/* 문자열 끝까지 DELIMITERS가 아닌 모든 문자를 건너뜁니다. */
	token = s;
	while (strchr (delimiters, *s) == NULL)
		s++;
	if (*s != '\0') {
		*s = '\0';
		*save_ptr = s + 1;
	} else
		*save_ptr = s;
	return token;
}

/* DST의 SIZE 바이트를 VALUE로 설정합니다. */
void *
memset (void *dst_, int value, size_t size) {
	unsigned char *dst = dst_;

	ASSERT (dst != NULL || size == 0);

	while (size-- > 0)
		*dst++ = value;

	return dst_;
}

/* 문자열 STRING의 길이를 반환합니다. */
size_t
strlen (const char *string) {
	const char *p;

	ASSERT (string);

	for (p = string; *p != '\0'; p++)
		continue;
	return p - string;
}

/* 만약 문자열 STRING이 MAXLEN보다 짧다면 실제 길이를 반환합니다.
   그렇지 않으면 MAXLEN을 반환합니다. */
size_t
strnlen (const char *string, size_t maxlen) {
	size_t length;

	for (length = 0; string[length] != '\0' && length < maxlen; length++)
		continue;
	return length;
}

/* 문자열 SRC를 DST에 복사합니다.
   SRC가 SIZE - 1보다 길 경우, SIZE - 1 글자만 복사됩니다.
   SIZE가 0이 아닌 경우 항상 null 종결자가 DST에 씌워집니다.
   null 종결자를 포함하지 않은 SRC의 길이를 반환합니다.

   strlcpy()는 표준 C 라이브러리에 포함되어 있지 않지만
   점차 인기를 얻고 있는 확장 함수입니다. 
   strlcpy()에 대한 정보는 아래 링크에서 확인할 수 있습니다:
   http://www.courtesan.com/todd/papers/strlcpy.html */
size_t
strlcpy (char *dst, const char *src, size_t size) {
	size_t src_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	if (size > 0) {
		size_t dst_len = size - 1;
		if (src_len < dst_len)
			dst_len = src_len;
		memcpy (dst, src, dst_len);
		dst[dst_len] = '\0';
	}
	return src_len;
}

/* 문자열 SRC를 DST에 연결합니다.
   연결된 문자열은 SIZE - 1 글자로 제한됩니다.
   SIZE가 0이 아닌 경우 항상 null 종결자가 DST에 씌워집니다.
   null 종결자를 포함하지 않은 연결된 문자열의 길이를 반환합니다.

   strlcat()는 표준 C 라이브러리에 포함되어 있지 않지만
   점차 인기를 얻고 있는 확장 함수입니다. 
   strlcat()에 대한 정보는 아래 링크에서 확인할 수 있습니다:
   http://www.courtesan.com/todd/papers/strlcpy.html */
size_t
strlcat (char *dst, const char *src, size_t size) {
	size_t src_len, dst_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	dst_len = strlen (dst);
	if (size > 0 && dst_len < size) {
		size_t copy_cnt = size - dst_len - 1;
		if (src_len < copy_cnt)
			copy_cnt = src_len;
		memcpy (dst + dst_len, src, copy_cnt);
		dst[dst_len + copy_cnt] = '\0';
	}
	return src_len + dst_len;
}

