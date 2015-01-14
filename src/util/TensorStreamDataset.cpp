/*
 * This file is part of the CN24 semantic segmentation software,
 * copyright (C) 2015 Clemens-Alexander Brust (ikosa dot de at gmail dot com).
 *
 * For licensing information, see the LICENSE file included with this project.
 */  

#include <fstream>

#include "Config.h"
#include "Dataset.h"
#include "Init.h"

#include "KITTIData.h"
#include "ConfigParsing.h"

namespace Conv {
datum DefaultLocalizedErrorFunction (unsigned int x, unsigned int y) {
  return 1;
}
TensorStreamDataset::TensorStreamDataset ( std::istream& training_stream, std::istream& testing_stream, unsigned int classes, std::vector< std::string > class_names, dataset_localized_error_function error_function ) :
  classes_ ( classes ), class_names_ ( class_names ) {
  LOGDEBUG << "Instance created.";

  // Count tensors
  Tensor tensor;

  while ( !training_stream.eof() ) {
    tensor.Deserialize ( training_stream );

    if ( tensor.elements() == 0 )
      break;

    // LOGDEBUG << "Tensor " << tensor_count_training_ << ": " << tensor;
    tensor_count_training_++;
  }

  LOGDEBUG << tensor_count_training_  / 2 << " training tensors";

  // We need alternating label and image tensors, so we need an even count
  if ( tensor_count_training_ & 1 ) {
    FATAL ( "Odd training tensor count!" );
  }

  while ( !testing_stream.eof() ) {
    tensor.Deserialize ( testing_stream );

    if ( tensor.elements() == 0 )
      break;

    // LOGDEBUG << "Tensor " << tensor_count_testing_ << ": " << tensor;
    tensor_count_testing_++;
  }

  LOGDEBUG << tensor_count_testing_ / 2 << " testing tensors";

  if ( tensor_count_testing_ & 1 ) {
    FATAL ( "Odd testing tensor count!" );
  }

  tensors_ = ( tensor_count_testing_ + tensor_count_training_ ) / 2;

  // Reset streams
  training_stream.clear();
  testing_stream.clear();
  training_stream.seekg ( 0, std::ios::beg );
  testing_stream.seekg ( 0, std::ios::beg );

  // Allocate arrays that depend on the tensor count
  data_ = new Tensor[tensors_];
  labels_ = new Tensor[tensors_];

  // Read tensors
  unsigned int e = 0;

  for ( unsigned int t = 0; t < ( tensor_count_training_ / 2 ); t++ ) {
    data_[t].Deserialize ( training_stream );
    labels_[t].Deserialize ( training_stream );
  }

  for ( unsigned int t = ( tensor_count_training_ / 2 ) ; t < tensors_; t++ ) {
    data_[t].Deserialize ( testing_stream );
    labels_[t].Deserialize ( testing_stream );
  }

  input_maps_ = data_[0].maps();
  label_maps_ = labels_[0].maps();
  
  // Prepare error cache
  error_cache.Resize(1, data_[0].width(), data_[0].height(), 1);
  for(unsigned int y = 0; y < data_[0].height(); y++) {
    for(unsigned int x = 0; x < data_[0].width(); x++) {
      *error_cache.data_ptr(x,y) = error_function(x,y);
    }
  }
  
  // System::viewer->show(&error_cache);
}

Task TensorStreamDataset::GetTask() const {
  return Task::SEMANTIC_SEGMENTATION;
}

unsigned int TensorStreamDataset::GetWidth() const {
  return data_[0].width();
}

unsigned int TensorStreamDataset::GetHeight() const {
  return data_[0].height();
}

unsigned int TensorStreamDataset::GetInputMaps() const {
  return input_maps_;
}

unsigned int TensorStreamDataset::GetLabelMaps() const {
  return label_maps_;
}

unsigned int TensorStreamDataset::GetClasses() const {
  return classes_;
}

std::vector<std::string> TensorStreamDataset::GetClassNames() const {
  return class_names_;
}

unsigned int TensorStreamDataset::GetTrainingSamples() const {
  return tensor_count_training_ / 2;
}

unsigned int TensorStreamDataset::GetTestingSamples() const {
  return tensor_count_testing_ / 2;
}

bool TensorStreamDataset::SupportsTesting() const {
  return tensor_count_testing_ > 0;
}

bool TensorStreamDataset::GetTrainingSample ( Tensor& data_tensor, Tensor& label_tensor, Tensor& weight_tensor, unsigned int sample, unsigned int index ) {
  if ( index < tensor_count_training_ / 2) {
    bool success = true;
    success &= Tensor::CopySample ( data_[index], 0, data_tensor, sample );
    success &= Tensor::CopySample ( labels_[index], 0, label_tensor, sample );
    success &= Tensor::CopySample (error_cache, 0, weight_tensor, sample);
    return success;
  } else return false;
}

bool TensorStreamDataset::GetTestingSample ( Tensor& data_tensor, Tensor& label_tensor, Tensor& weight_tensor, unsigned int sample, unsigned int index ) {
  if ( index < tensor_count_testing_ / 2) {
    bool success = true;
    unsigned int test_index = (tensor_count_training_ / 2) + index;
    success &= Tensor::CopySample ( data_[test_index], 0, data_tensor, sample );
    success &= Tensor::CopySample ( labels_[test_index], 0, label_tensor, sample );
    success &= Tensor::CopySample (error_cache, 0, weight_tensor, sample);
    return success;
  } else return false;
}

TensorStreamDataset* TensorStreamDataset::CreateFromConfiguration(std::istream& file) {
  unsigned int classes = 0;
  std::vector<std::string> class_names;
  dataset_localized_error_function error_function = DefaultLocalizedErrorFunction;
  std::string training_file;
  std::string testing_file;
  
  file.clear();
  file.seekg ( 0, std::ios::beg );
  while ( ! file.eof() ) {
    std::string line;
    std::getline ( file,line );
    
    if(StartsWithIdentifier(line, "classes")) {
      ParseCountIfPossible(line, "classes", classes);
      if(classes != 0) {
	for(int c = 0; c < classes; c++) {
	  std::string class_name;
	  std::getline(file,class_name);
	  class_names.push_back(class_name);
	}
      }
    }
    if(StartsWithIdentifier(line, "localized_error")) {
      std::string error_function_name;
      ParseStringIfPossible(line,"localized_error", error_function_name);
      if(error_function_name.compare("kitti") == 0) {
	LOGDEBUG << "Loading dataset with KITTI error function";
	error_function = KITTIData::LocalizedError;
      } else if (error_function_name.compare("default")) {
	LOGDEBUG << "Loading dataset with KITTI error function";
	error_function = DefaultLocalizedErrorFunction;
      }
    }
   
    ParseStringIfPossible(line, "training", training_file);
    ParseStringIfPossible(line, "testing", testing_file);
  }
  
  LOGDEBUG << "Loading dataset with " << classes << " classes";
  LOGDEBUG << "Training tensor: " << training_file;
  LOGDEBUG << "Testing tensor: " << testing_file;
  std::istream* training_stream = new std::ifstream(training_file, std::ios::in);
  std::istream* testing_stream = new std::ifstream(testing_file, std::ios::in);
  
  return new TensorStreamDataset(*training_stream, *testing_stream, classes,
				 class_names, error_function);
}

}
