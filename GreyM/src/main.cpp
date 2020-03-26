#include "pch.h"

#include "pe/portable_executable.h"
#include "utils/file_io.h"
#include "protector.h"

#pragma comment( lib, "capstone.lib" )

void PrintError( const std::string& message ) {
  printf( "[X] Error: %s LastError: %d", message.c_str(), GetLastError() );
}

int main( int argc, char* argv[] ) {
  // TODO: All the INT3 instructions between functions can be replaced with
  // BOGUS code to catch of disassemblers.

  try {
    srand( static_cast<unsigned int>( time( 0 ) ) );

    const std::string current_dir = argv[ 0 ];

    const std::string parent_dir =
        current_dir.substr( 0, current_dir.find_last_of( '\\' ) + 1 );

    const std::wstring parent_dir_wide =
        std::wstring( parent_dir.begin(), parent_dir.end() );

    const auto target_file_data = fileio::ReadBinaryFile(
        parent_dir_wide + TEXT( "Test Executable.exe" ) );

    if ( target_file_data.empty() ) {
      PrintError( "Unable to open the target file." );
      std::cin.get();
      return -1;
    }

    auto target_pe = pe::Open( target_file_data );

    if ( target_pe.IsValidPortableExecutable() ) {
      const auto new_protected_pe = protector::Protect( target_pe );
      if ( !fileio::WriteFileData(
               parent_dir_wide + TEXT( "Test Executable Out.exe" ),
               new_protected_pe.GetPeData() ) ) {
        PrintError( "Unable to write output file" );
      }
    } else {
      PrintError( "The PE is not valid." );
      std::cin.get();
      return -1;
    }

  } catch ( std::exception ex ) {
    PrintError( ex.what() );
  } catch ( ... ) {
    PrintError( "Unknown error occured" );
  }

  std::cin.get();

  return 0;
}