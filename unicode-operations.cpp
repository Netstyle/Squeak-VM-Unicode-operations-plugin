#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/coll.h>
#include <unicode/tblcoll.h>
#include <unicode/ucnv.h>

// Default EXPORT macro that does nothing (see comment in sq.h):
#define EXPORT(returnType) returnType

// Do not include the entire sq.h file but just those parts needed. The virtual machine proxy definition
#include "sqVirtualMachine.h"
// Configuration options
#include "sqConfig.h"
// Platform specific definitions
#include "sqPlatformSpecific.h"

#define true 1
#define false 0
#define nil 0


// Notes
//////////

// VM Version on Server: 4.0.3-2202 modified
//		This version identifies itself as 1.7 to plugins
//
// Tested also on 4.4.7.2357 (Ubuntu)
//		This version identifies itself as 1.8 to plugins


// Constants
//////////////

// 1.8 defines BASE_HEADER_SIZE which 1.7 does not have yet. ensure the constant exists
#ifndef BASE_HEADER_SIZE
#define BASE_HEADER_SIZE 4
#endif
#define SORT_STRENGTH_PRIMARY 0
#define SORT_STRENGTH_SECONDARY 1
#define SORT_STRENGTH_TERTIARY 2

// Function Prototypes
////////////////////////

static void registerPointer(void *pointer);
static void registerPointerInsteadOf(void *newPointer, void *oldPointer);
static void freePointers(void);
static sqInt stringOopFromCString(const char *string, sqInt length, sqInt *out);
static sqInt createCollator(const Locale &locale, int strength, sqInt errorOop, Collator **out);
static sqInt checkNumberOfArguments(int numberOfArguments);
static sqInt getErrorPointer(sqInt *out);
static void writeOutErrorCodeWithMessage(sqInt errorOop, UErrorCode errorCode, const char *errorMessage);
static void writeOutErrorMessage(sqInt errorOop, const char *errorMessage);
static sqInt createCollator(const Locale &locale, int strength, sqInt errorOop, Collator **out);
static size_t convertWideStringToByteChar(unsigned int *wideString, int wideStringSize, char *out);

#ifdef __cplusplus
extern "C" {
#endif
#pragma export on

EXPORT(const char *) getModuleName(void);
EXPORT(sqInt) setInterpreter(VirtualMachine *anInterpreter);
EXPORT(sqInt) primitiveIsPluginAvailable(void);
EXPORT(sqInt) primitiveCompareStrings(void);
EXPORT(sqInt) primitiveGetSortKey(void);
EXPORT(sqInt) primitiveConvertEncodings(void);
EXPORT(sqInt) primitiveConvertFromNFDtoNFC(void);

#pragma export off
#ifdef __cplusplus
}
#endif


// Variables
//////////////

VirtualMachine *vInterpreterProxy;
static const char *vModuleName = "NetstylePlugin 16 Maerz 2016 (e)";
typedef struct pointerRegistryStruct {
	void ** registry;
	int length;
	int limit;
} pointerRegistryStruct;
static pointerRegistryStruct *pointerRegistry;



// Squeak related functions
// This is coded so that plugins can be run from Squeak
/////////////////////////////////////////////////////////

EXPORT(sqInt) setInterpreter(VirtualMachine *anInterpreter) {
	sqInt success; // true or false
	vInterpreterProxy = anInterpreter;

	success = (vInterpreterProxy->majorVersion() == VM_PROXY_MAJOR);
	if(success == false){
		fprintf(stderr, "NetstylePlugin.setInterpreter: Version mismatch. Expected %i.%i but got %i.%i\n",
			VM_PROXY_MAJOR, VM_PROXY_MINOR, vInterpreterProxy->majorVersion(), vInterpreterProxy->minorVersion());
		return success;
	}

	success = (vInterpreterProxy->minorVersion() >= VM_PROXY_MINOR);
	if(success == false){
		fprintf(stderr, "NetstylePlugin.setInterpreter: Version mismatch. Expected %i.%i but got %i.%i\n",
			VM_PROXY_MAJOR, VM_PROXY_MINOR, vInterpreterProxy->majorVersion(), vInterpreterProxy->minorVersion());
		return success;
	}

	return success;
}

// This is hardcoded so it can be run from Squeak. The module name is used for validating
// a module *after* it is loaded to check if it does really contain the module we're
// thinking it contains. This is important!
EXPORT(const char *) getModuleName(void) {
	return vModuleName;
}


// Plugin code
/////////////////////////////////////////////////////////

static void registerPointer(void *pointer) {
	if(!pointerRegistry) {
		pointerRegistry = (pointerRegistryStruct*)calloc(1, sizeof(*pointerRegistry));
		pointerRegistry->registry = (void **)calloc(5, sizeof(pointerRegistry->registry));
		pointerRegistry->limit = 5;
	} else if(pointerRegistry->length + 1 == pointerRegistry->limit) {
		pointerRegistry->limit = pointerRegistry->limit + 5;
		pointerRegistry->registry = (void **)realloc(
			pointerRegistry->registry,
			pointerRegistry->limit * sizeof(pointerRegistry->registry));
	}
	pointerRegistry->registry[pointerRegistry->length++] = pointer;
}

static void registerPointerInsteadOf(void *newPointer, void *oldPointer) {
	if(!pointerRegistry) {
		registerPointer(newPointer);
	} else {
		for(int i = 0; i < pointerRegistry->length; i++) {
			if(pointerRegistry->registry[i] == oldPointer) {
				pointerRegistry->registry[i] = newPointer;
				return;
			}
		}
	}
}

static void freePointers(void) {
	if(pointerRegistry) {
		for(int i = 0; i < pointerRegistry->length; i++) {

			free(pointerRegistry->registry[i]);
		}
		free(pointerRegistry->registry);
		free(pointerRegistry);
		pointerRegistry = NULL;
	}
}

/*
 * Before using this function, the stack has to be emtpied or the existing stack
 * variables has to be saved by using pushRemappableOop() and popRemappableOop().
 * If not, the new allocated variable will be allocated somewhere within the existing variables
 */
static sqInt stringOopFromCString(const char *string, sqInt length, sqInt *out) {
	sqInt newString = vInterpreterProxy->instantiateClassindexableSize(vInterpreterProxy->classString(), length);
	strncpy((char *)vInterpreterProxy->arrayValueOf(newString), string, length);
	*out = newString;
	return !vInterpreterProxy->failed();
}

sqInt checkNumberOfArguments(int numberOfArguments) {
	return vInterpreterProxy->success(vInterpreterProxy->methodArgumentCount() == numberOfArguments);
}

sqInt getErrorPointer(sqInt *out) {
	sqInt errorOop = vInterpreterProxy->stackValue(0);
	*out = errorOop;
	return vInterpreterProxy->success(vInterpreterProxy->isBytes(errorOop));
}

void writeOutErrorCodeWithMessage(sqInt errorOop, UErrorCode errorCode, const char *errorMessage) {
	size_t strSize = vInterpreterProxy->stSizeOf(errorOop);
	char str[strSize];
	size_t lenErr = strlen(u_errorName(errorCode));
	if(lenErr > strSize){
		lenErr = strSize;
		strncpy((char*)vInterpreterProxy->arrayValueOf(errorOop), u_errorName(errorCode), strSize);
	} else {
		strncpy(str, u_errorName(errorCode), lenErr);
		size_t availableMemSize = strSize - lenErr;
		strncpy(str + lenErr, ": ", 2);
		strncpy(str + lenErr + 2, errorMessage, availableMemSize);
		strncpy((char*)vInterpreterProxy->arrayValueOf(errorOop), str, strSize);
	}
}

void writeOutErrorMessage(sqInt errorOop, const char *errorMessage) {
	size_t strSize = vInterpreterProxy->stSizeOf(errorOop);
	size_t len = strlen(errorMessage);
	if(len > strSize) {
		len = strSize;
	}
	strncpy((char*)vInterpreterProxy->arrayValueOf(errorOop), errorMessage, len);
}

sqInt createCollator(const Locale &locale, int strength, sqInt errorOop, Collator **out) {
	UErrorCode errorCode = U_ZERO_ERROR;
	Collator * collator = Collator::createInstance(locale, errorCode);

	if(U_FAILURE(errorCode)){
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Collator.createInstance failed");
		return false;
	}

	if(strength == SORT_STRENGTH_PRIMARY) {
		collator->setStrength(Collator::PRIMARY);
	} else if(strength == SORT_STRENGTH_SECONDARY) {
		collator->setStrength(Collator::SECONDARY);
	} else if(strength == SORT_STRENGTH_TERTIARY) {
		collator->setStrength(Collator::TERTIARY);
	} else {
		delete collator;
		writeOutErrorMessage(errorOop, (const char *)"Strength argument is invalid");
		return false;
	}

	collator->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, errorCode);
	if(U_FAILURE(errorCode)){
		delete collator;
		writeOutErrorMessage(errorOop, (const char *)"Collator.setAttribute failed");
		return false;
	}
	*out = collator;
	return true;
}

size_t convertWideStringToByteChar(unsigned int *wideString, int wideStringSize, char *out) {
	size_t charStringIndex = 0;
	for(int i = 0; i < wideStringSize; i++) {
		unsigned int word = wideString[i];
		for(int j = 3; j >= 0; j--) {
			 /*
			ignore 0 bytes. Either they are empty (most significant part) or
			they would terminate the string prematurely.
			*/
			char byte = (char) (word >> j * 8);
			if(byte) {
				out[charStringIndex++] = byte;
			}
		}
	}
	out[charStringIndex] = '\0';
	return charStringIndex;
}

// Primitives
/////////////////////////////////////////////////////////

EXPORT(sqInt) primitiveIsPluginAvailable(void) {
	return vInterpreterProxy->pushBool(true);
}

/**
 * Compare two utf8 strings using UAC. The five arguments contain the first and second string to compare,
 * the strength and the locale code and the error code holder.
 * Answers:
 * - 1 if string1 < string2,
 * - 2 if string1 = string2,
 * - 3 if string1 > string2
 */
EXPORT(sqInt) primitiveCompareStrings(void) {
	// arguments
	const int numberOfArguments = 5;
	sqInt string1Oop;
	const char *string1;
	sqInt string2Oop;
	const char *string2;
	int strength;
	sqInt localeCodeOop;
	const char *localeCode;
	sqInt errorOop;

	// other variables
	int string1Length;
	int string2Length;
	Locale locale;
	UErrorCode errorCode = U_ZERO_ERROR;
	Collator *collator;
	UnicodeString unicodeString1;
	UnicodeString unicodeString2;

	// retrieve the arguments
	if(!checkNumberOfArguments(numberOfArguments)) {
		return false;
	}
	if(!getErrorPointer(&errorOop)) {
		return false;
	}
	string1Oop = vInterpreterProxy->stackValue(4);
	string2Oop = vInterpreterProxy->stackValue(3);
	strength = vInterpreterProxy->stackIntegerValue(2);
	localeCodeOop = vInterpreterProxy->stackValue(1);

	vInterpreterProxy->success(vInterpreterProxy->isBytes(string1Oop));
	vInterpreterProxy->success(vInterpreterProxy->isBytes(string2Oop));
	vInterpreterProxy->success(vInterpreterProxy->isBytes(localeCodeOop));

	if(vInterpreterProxy->failed()) {
		writeOutErrorMessage(errorOop, (const char *)"Failed to retrieve arguments.");
		return false;
	}

	string1 = (const char *)vInterpreterProxy->firstIndexableField(string1Oop);
	string2 = (const char *)vInterpreterProxy->firstIndexableField(string2Oop);
	localeCode = (const char *)vInterpreterProxy->firstIndexableField(localeCodeOop);

	string1Length = vInterpreterProxy->stSizeOf(string1Oop);
	string2Length = vInterpreterProxy->stSizeOf(string2Oop);

	// sort using UAC. to speed up sorting large lists it would be better to create a method to obtain
	// the sort key (an array actually) to store alongside the String object
	locale = Locale(localeCode);
	if(locale.isBogus()){
		writeOutErrorMessage(errorOop, (const char *)"Locale.isBogus = false");
		return false;
	}

	if(!createCollator(locale, strength, errorOop, &collator)) {
		return false;
	}

	unicodeString1 = UnicodeString::fromUTF8(StringPiece(string1, string1Length));
	unicodeString2 = UnicodeString::fromUTF8(StringPiece(string2, string2Length));

	UCollationResult result = collator->compare(unicodeString1, unicodeString2, errorCode);
	if(U_FAILURE(errorCode)) {
		delete collator;
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *) "collator.compare failed");
		vInterpreterProxy->primitiveFail();
		return false;
	}

	vInterpreterProxy->pop(numberOfArguments);
	if(result == UCOL_LESS) {
		vInterpreterProxy->pushInteger(1);
	} else if(result == UCOL_EQUAL) {
		vInterpreterProxy->pushInteger(2);
	} else { // UCOL_GREATER
		vInterpreterProxy->pushInteger(3);
	}
	delete collator;
	return true;
}

/**
 * Retrieves the sort key for a string for sorting using UCA criterias. For this a byte string is
 * constructed suitable for binary string comparison. The strength indicates the comparison level
 * to retrieve the sort key for. Strength is 0 for the primary level, 1 for the secondary level
 * and 3 for the tertiary level.
 *
 * parameter 1: string to retrieve sort key for (String)
 * parameter 2: strength to use for the sort key (integer)
 * parameter 3: locale to use for the sort key (String)
 * parameter 4: error code holder (String)
 */
EXPORT(sqInt) primitiveGetSortKey(void) {
	// arguments
	const int numberOfArguments = 4;
	sqInt stringOop;
	const char *string;
	int strength;
	sqInt localeCodeOop;
	const char *localeCode;
	sqInt errorOop;

	// other variables
	int stringLength;
	UnicodeString sortKeyString;
	int sortKeyLength;
	sqInt resultStringOop;
	Locale locale;
	Collator * collator;

	// retrieve the arguments
	if(!checkNumberOfArguments(numberOfArguments)) {
		return false;
	}
	if(!getErrorPointer(&errorOop)) {
		return false;
	}
	stringOop = vInterpreterProxy->stackValue(3);
	strength = vInterpreterProxy->stackIntegerValue(2);
	localeCodeOop = vInterpreterProxy->stackValue(1);

	vInterpreterProxy->success(vInterpreterProxy->isBytes(stringOop));
	vInterpreterProxy->success(vInterpreterProxy->isBytes(vInterpreterProxy->stackValue(1)));

	if(vInterpreterProxy->failed()){
		writeOutErrorMessage(errorOop, (const char *)"Failed to retrieve arguments.");
		return false;
	}

	string = (const char *)vInterpreterProxy->firstIndexableField(stringOop);
	stringLength = vInterpreterProxy->stSizeOf(stringOop);
	localeCode = (const char *)vInterpreterProxy->firstIndexableField(localeCodeOop);

	// create the sort key for UCA sorting for the string
	locale = Locale(localeCode);
	if(locale.isBogus()) {
		writeOutErrorMessage(errorOop, (const char *) "Locale.isBogus = false");
		return vInterpreterProxy->primitiveFail();
	}

	if(!createCollator(locale, strength, errorOop, &collator)) {
		return false;
	}

	sortKeyString = UnicodeString::fromUTF8(StringPiece(string, stringLength));

	sortKeyLength = collator->getSortKey(sortKeyString, NULL, 0);
	if(sortKeyLength == 0) {
		delete collator;
		if(!stringOopFromCString("", 0, &resultStringOop)) {
			delete collator;
			return false;
		}
	} else {
		resultStringOop = vInterpreterProxy->instantiateClassindexableSize(vInterpreterProxy->classString(), sortKeyLength - 1);
		collator->getSortKey(sortKeyString, (uint8_t*)vInterpreterProxy->arrayValueOf(resultStringOop), sortKeyLength);
	}
	delete collator;
	vInterpreterProxy->popthenPush(numberOfArguments, resultStringOop);
}

EXPORT(sqInt) primitiveConvertEncodings(void) {
	// arguments
	int numberOfArguments = 4;
	sqInt inputStringOop;
	char *inputString;
	sqInt fromEncodingOop;
	char *fromEncoding;
	sqInt toEncodingOop;
	char *toEncoding;
	sqInt errorOop;

	// other variables
	char *fromEncodingCString;
	char *toEncodingCString;
	int fromEncodingLength;
	int toEncodingLength;
	char *inputCString;
	int inputStringLength;
	sqInt resultStringOop;
	char *resultString;
	sqInt resultStringLength;
	UErrorCode errorCode = U_ZERO_ERROR;
	char *stopPosition;

	// retrieve the arguments
	if(!checkNumberOfArguments(numberOfArguments)) {
		return false;
	}
	if(!getErrorPointer(&errorOop)) {
		return false;
	}
	inputStringOop = vInterpreterProxy->stackValue(3);
	fromEncodingOop = vInterpreterProxy->stackValue(2);
	toEncodingOop = vInterpreterProxy->stackValue(1);

	vInterpreterProxy->success(vInterpreterProxy->isKindOf(inputStringOop, (char *) "String"));
	vInterpreterProxy->success(vInterpreterProxy->isBytes(fromEncodingOop));
	vInterpreterProxy->success(vInterpreterProxy->isBytes(toEncodingOop));

	if(vInterpreterProxy->failed()) {
		writeOutErrorMessage(errorOop, (const char *)"Failed to read stack elements.");
		return false;
	}

	fromEncodingLength = vInterpreterProxy->stSizeOf(fromEncodingOop);
	toEncodingLength = vInterpreterProxy->stSizeOf(toEncodingOop);
	fromEncoding = (char *)vInterpreterProxy->firstIndexableField(fromEncodingOop);
	toEncoding = (char *)vInterpreterProxy->firstIndexableField(toEncodingOop);

	fromEncodingCString = (char *)calloc(fromEncodingLength + 1, sizeof(*fromEncoding));
	toEncodingCString = (char *)calloc(toEncodingLength + 1, sizeof(*toEncoding));
	registerPointer(fromEncodingCString);
	registerPointer(toEncodingCString);

	strncpy(fromEncodingCString, fromEncoding, fromEncodingLength);
	strncpy(toEncodingCString, toEncoding, toEncodingLength);

	UConverter *inputConverter = ucnv_open(toEncodingCString, &errorCode);
	if(U_FAILURE(errorCode)) {
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Cannot convert to given encoding name");
		freePointers();
		vInterpreterProxy->primitiveFail();
		return false;
	}

	UConverter *outputConverter = ucnv_open(fromEncodingCString, &errorCode);
	if (U_FAILURE(errorCode)) {
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Cannot convert from given encoding name");
		ucnv_close(inputConverter);
		freePointers();
		vInterpreterProxy->primitiveFail();
		return false;
	}

	inputStringLength = vInterpreterProxy->stSizeOf(inputStringOop);
	// ByteString / WideString
	if(vInterpreterProxy->isBytes(inputStringOop)) {
		resultStringLength = 2 * inputStringLength;
		char *byteString = (char *)vInterpreterProxy->firstIndexableField(inputStringOop);
		inputCString = (char *)calloc(inputStringLength + 1, sizeof(*inputCString));
		strncpy(inputCString, byteString, inputStringLength);
	} else {
		resultStringLength = 2 * 4 * inputStringLength;
		unsigned int *wideString = (unsigned int *)vInterpreterProxy->firstIndexableField(inputStringOop);
		inputCString = (char *)calloc(4 * inputStringLength + 1, sizeof(*inputString));
		inputStringLength = convertWideStringToByteChar(wideString, inputStringLength, inputCString);
	}
	resultString = (char *)calloc(resultStringLength, sizeof(*resultString));
	registerPointer(inputCString);
	registerPointer(resultString);

	// let another pointer run: stopPosition - resultString would return the size of the resulting encoded string
	stopPosition = resultString;
	ucnv_convertEx(inputConverter, outputConverter,
				&stopPosition, (const char *)resultString + resultStringLength,
				(const char **)&inputCString, (const char *)inputCString + inputStringLength,
				NULL, NULL, NULL, NULL,
				TRUE, TRUE,
				&errorCode);

	if(U_FAILURE(errorCode)) {
		if(errorCode == U_BUFFER_OVERFLOW_ERROR) {
			errorCode = U_ZERO_ERROR;
			resultStringLength *= 2;
			char *newResultString = (char *)realloc(resultString, resultStringLength + 1);
			registerPointerInsteadOf(newResultString, resultString);
			resultString = newResultString;

			// let another pointer run: stopPosition - resultString would return the size of the resulting encoded string
			stopPosition = resultString;
			ucnv_convertEx(inputConverter, outputConverter,
				&stopPosition, (const char *)resultString + inputStringLength,
				(const char **)&inputCString, inputCString + inputStringLength,
				NULL, NULL, NULL, NULL,
				TRUE, TRUE,
				&errorCode);
		}

		if (U_FAILURE(errorCode)) {
			writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Conversion failed");
			vInterpreterProxy->primitiveFail();
		} else {
			if(!stringOopFromCString(resultString, stopPosition - resultString, &resultStringOop)) {
				return false;
			}
			vInterpreterProxy->popthenPush(numberOfArguments, resultStringOop);
		}
	} else {
		if(!stringOopFromCString(resultString, stopPosition - resultString, &resultStringOop)) {
			return false;
		}
		vInterpreterProxy->popthenPush(numberOfArguments, resultStringOop);
	}

	ucnv_close(inputConverter);
	ucnv_close(outputConverter);
	freePointers();
	return vInterpreterProxy->failed();
}

EXPORT(sqInt) primitiveConvertFromNFDtoNFC(void) {
	// arguments
	int numberOfArguments = 2;
	sqInt inputStringOop;
	sqInt errorOop;

	// other variables
	char *inputCString;
	sqInt inputStringLength;
	sqInt sourceUCharOop;
	sqInt destUCharOop;
	sqInt resultStringOop;
	char *resultString;
	int resultStringLength;
	UErrorCode errorCode = U_ZERO_ERROR;
	UChar *sourceUChar;
	UChar *destUChar;
	int sourceUCharCapacity;
	int sourceUCharLength;
	int destUCharCapacity;
	int destUCharLength;
	const UNormalizer2 *normalizer;

	if(!checkNumberOfArguments(numberOfArguments)) {
		return false;
	}
	if(!getErrorPointer(&errorOop)) {
		return false;
	}
	inputStringOop = vInterpreterProxy->stackValue(1);

	vInterpreterProxy->success(vInterpreterProxy->isKindOf(inputStringOop, (char *) "String"));

	if (vInterpreterProxy->failed()) {
		writeOutErrorMessage(errorOop, (const char *)"Input is not a String.");
		return false;
	}

	inputStringLength = vInterpreterProxy->stSizeOf(inputStringOop);
	// ByteString / WideString
	if(vInterpreterProxy->isBytes(inputStringOop)) {
		char *byteString = (char *)vInterpreterProxy->firstIndexableField(inputStringOop);
		inputCString = (char *)calloc(inputStringLength + 1, sizeof(*inputCString));
		strncpy(inputCString, byteString, inputStringLength);
	} else {
		unsigned int *wideString = (unsigned int *)vInterpreterProxy->firstIndexableField(inputStringOop);
		inputCString = (char *)calloc(4 * inputStringLength + 1, sizeof(*inputCString));
		inputStringLength = convertWideStringToByteChar(wideString, inputStringLength, inputCString);
	}
	registerPointer(inputCString);

	// allocate more than enough space to compensate for expansions
	sourceUCharCapacity = 2 * inputStringLength;
	destUCharCapacity = sourceUCharCapacity;
	sourceUChar = (UChar *)calloc(sourceUCharCapacity + 1, sizeof(*sourceUChar));
	destUChar = (UChar *)calloc(destUCharCapacity + 1, sizeof(*destUChar));
	// assume that the normalised string will be <= original string
	// we handle the other case in the buffer overflow fallback
	resultString = (char *)calloc(inputStringLength + 1, sizeof(*resultString));
	registerPointer(sourceUChar);
	registerPointer(destUChar);
	registerPointer(resultString);



	u_strFromUTF8(sourceUChar, sourceUCharCapacity, &sourceUCharLength, inputCString, inputStringLength, &errorCode);
	if(U_FAILURE(errorCode)) {
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Conversion of char to UChar failed");
		freePointers();
		vInterpreterProxy->primitiveFail();
		return false;
	}

	normalizer = unorm2_getNFCInstance(&errorCode);
	destUCharLength = unorm2_normalize(normalizer, sourceUChar, sourceUCharLength, destUChar, destUCharCapacity, &errorCode);
	if (U_FAILURE(errorCode)) {
		writeOutErrorMessage(errorOop, (const char *)"Failed to normalize string to NFC");
		freePointers();
		vInterpreterProxy->primitiveFail();
		return false;
	}

	UConverter *inputConverter = ucnv_open("UTF-8", &errorCode);
	if (U_FAILURE(errorCode)) {
		writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Conversion to UTF-8 failed.");
		freePointers();
		vInterpreterProxy->primitiveFail();
		return false;
	}

	resultStringLength = ucnv_fromUChars(inputConverter, resultString, inputStringLength, destUChar, destUCharLength, &errorCode);
	if (U_FAILURE(errorCode)) {
		// we *do* need this fallback since normalisation can in special cases
		// yield more bytes (see http://www.macchiato.com/unicode/nfc-faq)
		if(errorCode == U_BUFFER_OVERFLOW_ERROR) {
			errorCode = U_ZERO_ERROR;
			inputStringLength *= 2;
			char * newresultString = (char *)realloc(resultString, inputStringLength + 1);
			registerPointerInsteadOf(newresultString, resultString);
			resultString = newresultString;
			ucnv_fromUChars(inputConverter, resultString, inputStringLength, destUChar, destUCharLength, &errorCode);
		}
		if (U_FAILURE(errorCode)) {
			writeOutErrorCodeWithMessage(errorOop, errorCode, (const char *)"Conversion of UTF-8 back to char failed");
			ucnv_close(inputConverter);
			freePointers();
			vInterpreterProxy->primitiveFail();
			return false;
		}
	}

	ucnv_close(inputConverter);
	if(!stringOopFromCString(resultString, resultStringLength, &resultStringOop)) {
		freePointers();
		return false;
	}

	vInterpreterProxy->popthenPush(numberOfArguments, resultStringOop);
	freePointers();
	return true;
}
