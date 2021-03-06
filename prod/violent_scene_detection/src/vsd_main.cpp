#include <iostream>
#include <fstream>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include "optionparser.h"
#include "ViolenceModel.h"
#include "ImageUtil.h"

 // Path to index file.
static boost::filesystem::path indexFilePath;
static boost::filesystem::path predictionFilePath;
static long xval_k = 10;

// Parses the input file and indexes the paths if they're valid.
bool process_index_file(boost::filesystem::path path, ViolenceModel &model);

// Validates -f, --index-file option and consumes the path.
option::ArgStatus checkFileArg(const option::Option& option, bool msg);

option::ArgStatus checkXValArg(const option::Option& option, bool msg);

// http://optionparser.sourceforge.net/
 enum  optionIndex { UNKNOWN, INDEX_FILE, TRAIN, CLEAR, PREDICT, ERROR, XVAL };
 const option::Descriptor usage[] =
 {
  {UNKNOWN,    0, "",  "",			     option::Arg::None, "USAGE: example [options]\n\n""Options:" },
  {INDEX_FILE, 0, "f", "index-file",     &checkFileArg,     "--index-file, -f <file_path>  Index the videos specified in file." },
  {TRAIN,      0, "t", "train",          option::Arg::None, "--train, -t  Train the model with the existing index." },
  {CLEAR,      0, "c", "clear",          option::Arg::None, "--clear, -c  Clear the index store before respecting any other options." },
  {PREDICT,    0, "p", "predict",        &checkFileArg,     "--predict, -p <file_path> Use the learned model to predict violent content in the video at <file_path>." },
  {ERROR,      0, "e", "error",          option::Arg::None, "--error, -e Compute the training, cross validation, and testing error." },
  {XVAL,       0, "x", "cross-validate", &checkXValArg, "--cross-validate, -x <k> Run k-fold cross validation to evaluate the model." },
  {0,0,0,0,0,0}
 };

ViolenceModel vm;

void terminated(int signum)
{
	std::cout << "Received SIGTERM \n";
	//vm.persistStore();
}

int main(int argc, char* argv[]) {

	// the old random dance.
	std::srand( std::time(NULL) );

	//	struct sigaction action = {0};
	//	action.sa_handler = terminated;
	//	sigaction(SIGTERM, &action, NULL);

	// Set up command line arg parser.
	option::Stats  stats(usage, argc, argv);

	// The sizes are just suitable maximums.
	// Trying to declare them on the heap using the sizes from the example crashes.
	// The example code tries to use the options_max and buffer_max values to initialize
	// stack arrays.  This is illegal as the size isn't known at compile time.  Not sure
	// why it would be included as a sample as it clearly doesn't build.
	option::Option options[256];
	option::Option buffer[256];

	// Create the parser in GNU mode (true as first argument).
	option::Parser parser(true, usage, argc, argv, options, buffer);

	if ( parser.error() ) return 1;

	if ( options[CLEAR] ) {
		vm.clear();
	}

	if ( !indexFilePath.empty() && !process_index_file(indexFilePath, vm) ) {
		std::cout << "process_index_file failed. Aborting.\n";
		return 2;
	}

	if ( options[XVAL] ) {
		vm.crossValidate(xval_k);
		vm.graciaCrossValidate(xval_k);
	}

	if (  options[TRAIN] ) {
		vm.train();
	}

	if ( options[ERROR] || options[TRAIN] ) {
//		vm.computeError(ViolenceModel::TRAINING);
//		vm.computeError(ViolenceModel::X_VALIDATION);
//		vm.computeError(ViolenceModel::TESTING);
	}

	if ( options[PREDICT] ) {
		std::string predictArg = options[PREDICT].arg;
		vm.predict(predictArg, 2);
	}

    return 0;
}

bool process_index_file(boost::filesystem::path path, ViolenceModel &model)
{
	if ( !boost::filesystem::exists(path) ) {
		std::cout << "Index file doesn't exist.\n";
		return false;
	}

	std::string line;
	uint lineNumber = 0;
	std::ifstream inFile(path.generic_string());

	// Check whether the string is compatible with our istream expectations.
	const boost::regex e("^\\s*(([01][\\s.]*)|(#)).*$");

	while ( std::getline(inFile, line) )
	{
		lineNumber++;
		line = boost::trim_copy(line);

		// If not, print an error message and move to the next line.
		if ( !boost::regex_match(line, e) ) {
			// TODO: It would be nice to give a more self explanatory error here.  If there were time.
			std::cout << "input file -> line " << lineNumber << " is malformed.  Ignoring: " << line << "\n";
			continue;
		}

		// Only process the line if it's not a comment.
		if ( !boost::starts_with(line, "#") ) {

			// Parse the line using an input stringstream.
			std::istringstream inStr(line);

			bool isViolent = false;
			inStr >> isViolent;

			std::string videoPathStr;
			inStr >> videoPathStr;

			// Good programmers always have doubt.
			std::cout << "input file -> " << " isViolent: " << isViolent << " path: " << videoPathStr << "\n";

			// Wrap a path object around the string.
			boost::filesystem::path videoPath = boost::filesystem::absolute(videoPathStr);
			std::vector<std::string> pathsToIndex;

			// If we've been given a path to a directory, then get load all files for indexing.
			if ( boost::filesystem::is_directory(videoPath) ) {

				std::cout << "input file -> path at line " << lineNumber << " is a directory. Adding children. \n";
				// Only accept files.  Don't recurse child directories.  Perhaps in v2.
				boost::filesystem::directory_iterator endIter;
				for ( boost::filesystem::directory_iterator iter(videoPath); iter != endIter; iter++ ) {

					if ( !boost::filesystem::is_directory( iter->path().string() ) ) {
						//std::cout<< "child: " << iter->path().string() << "\n";
						pathsToIndex.push_back(iter->path().string());
					} else {
						std::cout << "input file -> child path is a directory.  Skipping it.\n";
					}
				}

			} else {
				pathsToIndex.push_back(videoPathStr);
			}

			BOOST_FOREACH(std::string pathStr, pathsToIndex)
            {
				cv::VideoCapture vc;
				// TODO: Many "videos" used in computer vision research are actually a shorthand format string given to opencv
				//       that specifies the file name format (eg. img_%02d.jpg -> img_00.jpg, img_01.jpg, img_02.jpg, ...).
				//       Since these strings are more difficult to parse, we can simply attempt a file open first.
				//       That way the file path can be compatible with this feature as well.  Hopefully this isn't too expensive.
				if ( model.isIndexed(/*target,*/ pathStr) ) {
					//std::cout << "process_index_file -> skipping indexed path: " << pathStr << "\n";
                } else if ( vc.open(pathStr) ) {
                    // Woohoo!!
                    model.index(pathStr, isViolent);
                    model.persistStore();
                } else {
                    std::cout << "couldn't open file for indexing.\n";
                }
            }
		}
	}

	return true;
}

option::ArgStatus checkFileArg(const option::Option& option, bool msg)
{
	std::cout <<"option " << option.name << "\n";

	// Determine which filePath we want to populate.
	boost::filesystem::path *filePath = NULL;
	if ( option.index() == INDEX_FILE ) {
		filePath = &indexFilePath;
	} else if ( option.index() == PREDICT  ) {
		filePath = &predictionFilePath;
	}

	// Only set the input file path if it hasn't been set yet and the argument isn't empty.
	if ( strcmp(option.arg, "") != 0 && filePath->empty() ) {
		boost::filesystem::path tmpPath = boost::filesystem::absolute(option.arg);
		if ( boost::filesystem::exists(tmpPath) ) {
			std::cout << "consumed file argument: "<< filePath->generic_string() << "\n";
			*filePath = tmpPath;
			return option::ARG_OK;
		}
	}

	if ( msg ) {
		const char * pathCStr = option.arg ? option.arg : "<empty>";
		std::cout<<"Index file at path " << pathCStr << " does not exist. Aborting.\n";
		option::printUsage(std::cout, usage);
	}

	return option::ARG_ILLEGAL;
}

option::ArgStatus checkXValArg(const option::Option& option, bool msg)
{
	std::cout <<"verifying option: " << option.name << "\n";
	if ( option.arg ) {

		xval_k = strtol(option.arg, NULL, 0);

		if ( xval_k <= 0 ) {
			if ( msg ) {
				std::cout << "cross validation K must be a non-zero positive integer or empty to assume default. " << option.arg << " given.\n";
				option::printUsage(std::cout, usage);
			}

			return option::ARG_ILLEGAL;
		}
	}

	std::cout << "updating cross validation argument to " << xval_k << "\n";
	return option::ARG_OK;
}


