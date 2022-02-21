// This module implements symbol helpers, to deal with .pdb files and extract symbol names for labels etc.
// 
module;

#include "VirtualizerBase.h"
#include <dia2.h>
#include <filesystem>

export module SymbolHelp;
import ImageHelp;

namespace filesystem = std::filesystem;

#define MAKE_SYMHELP_HRESULT(Severity, StatusCode)\
	(((Severity) << 31) | (1 << 29) | ((StatusCode & 0xffff)))
export class SymbolException
	: public CommonExceptionType {
public:
	enum ExceptionCodes : HRESULT {
		EXCEPTION_CANNOT_BE_NON_STATIC = MAKE_SYMHELP_HRESULT(SEVERITY_ERROR, 1),
		EXCEPTION_FAILED_TO_FETCH_TYPE = MAKE_SYMHELP_HRESULT(SEVERITY_ERROR, 2),
		EXCEPTION_FAILED_RVA_FOR_SYMBOL = MAKE_SYMHELP_HRESULT(SEVERITY_ERROR, 3),



		EXCEPTION_INVALID_EXCEPTION = MAKE_SYMHELP_HRESULT(SEVERITY_SUCCESS, 0)
	};

	SymbolException(
		IN const std::string_view& ExceptionText,
		IN       HRESULT           ComStatusCode
	)
		: CommonExceptionType(ExceptionText,
			ComStatusCode,
			CommonExceptionType::EXCEPTION_COMOLE_EXP) {
		TRACE_FUNCTION_PROTO;
	}

};


export class SymbolHelp {
public:
	SymbolHelp(
		IN const filesystem::path& PdbExeFileName,
		IN       bool              LoadAsPdbOverwrite = false,
		IN const filesystem::path& PdbSearchPath = {}
	) {
		TRACE_FUNCTION_PROTO;

		// Instantiate DIA through COM device
		auto ComResult = DiaSource.CoCreateInstance(CLSID_DiaSource,
			nullptr,
			CLSCTX_INPROC_SERVER);
		if (FAILED(ComResult))
			throw SymbolException(
				fmt::format("Failed to create DIASOURCE instance with " ESC_BRIGHTRED"{}" ESC_RESET
					" : \"" ESC_BRIGHTRED"{}\"",
					ComResult,
					FormatWin32ErrorMessage(ComResult)),
				ComResult);

		// Check filenames manually, due to a bug in msdia causing a file handle leak
		if (!PdbExeFileName.extension().compare(".pdb"))
			LoadAsPdbOverwrite = true;
		if (!LoadAsPdbOverwrite) {

			// Failed to open file, possibly not a PDB, try to open it as an executable image?
			// The path was not a pdb ?, try to load it as an executable			
			ComResult = DiaSource->loadDataForExe(PdbExeFileName.c_str(),
				PdbSearchPath.empty() ? nullptr
					: PdbSearchPath.c_str(),
				nullptr);
			switch (ComResult) {
			case S_OK:
				break;
			case E_PDB_NOT_FOUND:
				throw std::runtime_error("failed to find pdb for executable (automatic mode)");
			case E_PDB_INVALID_SIG:
			case E_PDB_INVALID_AGE:
				throw std::runtime_error("could not load pdb, signature and or age did not match executable");
			default:
				throw std::runtime_error("failed to load pdb for executable (automatic mode) unspecified error");
			}

		}
		else {

			// File is a pdb by extension or override, load as is
			ComResult = DiaSource->loadDataFromPdb(PdbExeFileName.c_str());
			switch (ComResult) {
			case S_OK:
				break;
			case E_PDB_NOT_FOUND:
			case E_PDB_FORMAT:
				SPDLOG_WARN("loadDataFromPdb failed to load " ESC_BRIGHTYELLOW"\"{}\"" ESC_RESET" as pdb,\n"
					"due to a bug in msdia, this may have leaked a file handle to said file,\n"
					"resulting in the file being effectively locked up till the end of thise software.\n"
					"Restarting the application may be advised if it refuses to reload the any FILE.",
					PdbExeFileName.string().c_str());
				throw SymbolException("symbol FAILED to load, check previous for more data",
					ComResult);

			default:
				throw std::runtime_error("failed to load pdb (manual mode), unspecified");
			}
		}

		ComResult = DiaSource->openSession(&DiaSession);
		ComResult = DiaSession->get_globalScope(&DiaSymbols);
		if (FAILED(ComResult))
			throw std::runtime_error("failed to load pdb data, open dia session, and load symbls");
	}
	~SymbolHelp() {
		TRACE_FUNCTION_PROTO;

		// BUG: unloading a file doesnt seem to release the file handle when symbols are loaded, 
		//      idk why yet, theoratically this should be treated by the smart pointers

		// BUG: EDIT: seems to be actually msdia, and still no idea why, its retarded that i have to close the file after reading it
		//            just to access it with msdia anyways, so im just gonna fuck of the loadDataForExe api support
		//            and locate the fucking pdb file myself, cba to fucking fix their tools
		
		// EDIT: the leak is caused by a failing call to loadDataFormPdb, this basically result in a ReadOnly handle leak, 
		//       this bug wasnt noticed for a long time so good me has to rewrite a good few chunks here, such as:
		//       The Constructors of this class, as well as the Reject mechanism in ImageHelp.ixx as thats obsolete now,
		//       being a pure workaround for what i deemed was a stupid design choice

		// EDIT: bug should be circumvented now
	}

	void InstallPdbFileMappingVirtualAddress(
		IN const IImageLoaderHelp* VirtualImage
	) {
		TRACE_FUNCTION_PROTO;

		auto ComResult = DiaSession->put_loadAddress(
			reinterpret_cast<uintptr_t>(VirtualImage->GetImageFileMapping()));
		if (ComResult != S_OK)
			throw std::runtime_error("did not set imagebase for symbol translation");
	}

	byte_t* FindAddressForSymbol(
		IN const std::string_view& SymbolName,
		IN enum  SymTagEnum        SymbolType = SymTagNull
	) {
		TRACE_FUNCTION_PROTO;

		auto SymbolName2 = ConvertAnsiToUnicode(SymbolName);

		CComPtr<IDiaEnumSymbols> SymbolIterator;
		auto ComResult = DiaSymbols->findChildrenEx(SymbolType,
			SymbolName2.c_str(),
			nsfCaseSensitive,
			&SymbolIterator);
		if (ComResult != S_OK)
			throw std::runtime_error("FindChildrenEx failed to locate symbols");

		CComPtr<IDiaSymbol> Symbol;
		ulong_t Fetched;
		ComResult = SymbolIterator->Next(1,
			&Symbol,
			&Fetched);
		if (ComResult != S_OK || Fetched != 1)
			throw std::runtime_error("failed to fetch next symbol");

		ulong_t LocationType;
		ComResult = Symbol->get_locationType(&LocationType);
		if (ComResult != S_OK)
			throw std::runtime_error("failed to fetch loctype");

		if (LocationType != LocIsStatic)
			return nullptr;

		byte_t* VirtualAddress = nullptr;
		ComResult = Symbol->get_relativeVirtualAddress(
			reinterpret_cast<ulong_t*>(&VirtualAddress));
		if(ComResult != S_OK)
			throw std::runtime_error("failed to gen address");

		uintptr_t ImageBase;
		DiaSession->get_loadAddress(&ImageBase);
		return VirtualAddress + ImageBase;
	}
	std::pair<byte_t*, size_t> FindFunctionFrameByName(
		IN const std::string_view& FunctionNameDecorated
	) {
		TRACE_FUNCTION_PROTO;

		auto FunctionNameDecorated2 = ConvertAnsiToUnicode(FunctionNameDecorated);

		CComPtr<IDiaEnumSymbols> SymbolIterator;
		auto ComResult = DiaSymbols->findChildrenEx(SymTagFunction,
			FunctionNameDecorated2.c_str(),
			nsfCaseSensitive,
			&SymbolIterator);
		if (ComResult != S_OK)
			throw std::runtime_error("FindChildrenEx failed to locate symbols");

		CComPtr<IDiaSymbol> Symbol;
		ulong_t Fetched;
		ComResult = SymbolIterator->Next(1,
			&Symbol,
			&Fetched);
		if (ComResult != S_OK || Fetched != 1)
			throw std::runtime_error("failed to fetch next symbol");

		ulong_t LocationType;
		ComResult = Symbol->get_locationType(&LocationType);
		if (ComResult != S_OK)
			throw std::runtime_error("failed to fetch loctype");
		if (LocationType != LocIsStatic)
			return {};

		byte_t* VirtualAddress = nullptr;
		ComResult = Symbol->get_relativeVirtualAddress(
			reinterpret_cast<ulong_t*>(&VirtualAddress));
		if (ComResult != S_OK)
			throw std::runtime_error("failed to gen address");

		size_t VirtualSize;
		ComResult = Symbol->get_length(&VirtualSize);
		if (ComResult != S_OK)
			throw std::runtime_error("failed to get lenght");

		uintptr_t ImageBase;
		DiaSession->get_loadAddress(&ImageBase);
		return std::make_pair(VirtualAddress + ImageBase, VirtualSize);
	}


	std::string FindSymbolForAddress(
		IN      byte_t*    VirtualAddress,
		IN enum SymTagEnum SymbolType = SymTagNull
	) {
		TRACE_FUNCTION_PROTO;

		CComPtr<IDiaSymbol> Symbol;
		disp_t Displacment = 0;
		auto ComResult = DiaSession->findSymbolByVAEx(reinterpret_cast<uintptr_t>(VirtualAddress),
			SymbolType,
			&Symbol,
			&Displacment);
		if (FAILED(ComResult))
			throw std::runtime_error("FindSymbolByVAEx failed");

		BSTR SymbolString = nullptr;
		ComResult = Symbol->get_name(&SymbolString);
		auto SymbolString2 = ConvertUnicodeToAnsi(SymbolString);
		SysFreeString(SymbolString);
		return SymbolString2;
	}


	struct FunctionEnumerator {
		byte_t* VirtualAddress;
		size_t  VirtualAsize;
		std::string FunctionName;
	};
	std::vector<FunctionEnumerator>
	GenerateMappingOfFunctions() {
		TRACE_FUNCTION_PROTO;

		CComPtr<IDiaEnumSymbols> SymbolIterator;
		auto ComResult = DiaSymbols->findChildrenEx(SymTagFunction,
			nullptr,
			nsNone,
			&SymbolIterator);
		if (ComResult != S_OK)
			throw std::runtime_error("FindChildrenEx failed to locate symbols");

		CComPtr<IDiaSymbol> Symbol;
		ulong_t Fetched;
		std::vector<FunctionEnumerator> FunctionList;
		while (ComResult = SymbolIterator->Next(1,
			&Symbol,
			&Fetched) == S_OK && Fetched == 1) {

			ulong_t LocationType;
			ComResult = Symbol->get_locationType(&LocationType);
			if (ComResult != S_OK)
				throw SymbolException("Failed to fetch location type",
					SymbolException::EXCEPTION_FAILED_TO_FETCH_TYPE);
			if (LocationType != LocIsStatic)
				throw SymbolException("Location cannot be outside of static",
					SymbolException::EXCEPTION_CANNOT_BE_NON_STATIC);

			byte_t* VirtualAddress = nullptr;
			ComResult = Symbol->get_relativeVirtualAddress(
				reinterpret_cast<ulong_t*>(&VirtualAddress));
			if (ComResult != S_OK)
				throw SymbolException("Failed to get rva in module for symbol",
					SymbolException::EXCEPTION_FAILED_RVA_FOR_SYMBOL);

			uintptr_t ImageBase;
			DiaSession->get_loadAddress(&ImageBase);
			VirtualAddress += ImageBase;

			// TODO: continue codiiiiiiing here mister limo...

		}
		
		return FunctionList;
	}


private:
	CComPtr<IDiaDataSource> DiaSource;
	CComPtr<IDiaSession>    DiaSession;
	CComPtr<IDiaSymbol>     DiaSymbols;
};



