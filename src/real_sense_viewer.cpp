/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2014-, Open Perception, Inc.
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the copyright holder(s) nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <iostream>

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>

#include <pcl/console/print.h>
#include <pcl/console/parse.h>
#include <pcl/common/time.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/filters/fast_bilateral.h>
#include <pcl/io/io_exception.h>
#include <pcl/io/pcd_io.h>

#include "real_sense_grabber.h"
#include "real_sense/real_sense_device_manager.h" // TODO: remove afterwards

using namespace pcl::console;

void
printHelp (int, char **argv)
{
  std::cout << std::endl;
  std::cout << "****************************************************************************" << std::endl;
  std::cout << "*                                                                          *" << std::endl;
  std::cout << "*                        REAL SENSE VIEWER - Usage Guide                   *" << std::endl;
  std::cout << "*                                                                          *" << std::endl;
  std::cout << "****************************************************************************" << std::endl;
  std::cout << std::endl;
  std::cout << "Usage: " << argv[0] << " [Options] device_id" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << std::endl;
  std::cout << "     --help, -h : Show this help"                                             << std::endl;
  std::cout << "     --list, -l : List connected RealSense devices"                           << std::endl;
  std::cout << "     --xyz      : View XYZ-only clouds"                                       << std::endl;
  std::cout << std::endl;
  std::cout << "Keyboard commands:"                                                           << std::endl;
  std::cout << std::endl;
  std::cout << "   When the focus is on the viewer window, the following keyboard commands"   << std::endl;
  std::cout << "   are available:"                                                            << std::endl;
  std::cout << "     * t/T : increase or decrease depth data confidence threshold"            << std::endl;
  std::cout << "     * k   : enable next temporal filtering method"                           << std::endl;
  std::cout << "     * b   : toggle bilateral filtering"                                      << std::endl;
  std::cout << "     * a/A : increase or decrease bilateral filter spatial sigma"             << std::endl;
  std::cout << "     * z/Z : increase or decrease bilateral filter range sigma"               << std::endl;
  std::cout << "     * s   : save the last grabbed cloud to disk"                             << std::endl;
  std::cout << "     * h   : print the list of standard PCL viewer commands"                  << std::endl;
  std::cout << std::endl;
  std::cout << "Notes:"                                                                       << std::endl;
  std::cout << std::endl;
  std::cout << "   The device to grab data from is selected using device_id argument. It"     << std::endl;
  std::cout << "   could be either:"                                                          << std::endl;
  std::cout << "     * serial number (e.g. 231400041-03)"                                     << std::endl;
  std::cout << "     * device index (e.g. #2 for the second connected device)"                << std::endl;
  std::cout << std::endl;
  std::cout << "   If device_id is not given, then the first available device will be used."  << std::endl;
  std::cout << std::endl;
}

void
printDeviceList ()
{
  typedef boost::shared_ptr<pcl::RealSenseGrabber> RealSenseGrabberPtr;
  std::vector<RealSenseGrabberPtr> grabbers;
  std::cout << "Connected devices: ";
  boost::format fmt ("\n  #%i  %s");
  while (true)
  {
    try
    {
      grabbers.push_back (RealSenseGrabberPtr (new pcl::RealSenseGrabber));
      std::cout << boost::str (fmt % grabbers.size () % grabbers.back ()->getDeviceSerialNumber ());
    }
    catch (pcl::io::IOException& e)
    {
      break;
    }
  }
  if (grabbers.size ())
    std::cout << std::endl;
  else
    std::cout << "none" << std::endl;
}

template <typename PointT>
class RealSenseViewer
{

  public:

    typedef pcl::PointCloud<PointT> PointCloudT;

    RealSenseViewer (pcl::RealSenseGrabber& grabber)
    : grabber_ (grabber)
    , viewer_ ("RealSense Viewer")
    , window_ (3)
    , threshold_ (6)
    , temporal_filtering_ (pcl::RealSenseGrabber::RealSense_None)
    , with_bilateral_ (false)
	, save_stream_(false)
	, stream_id_(0)
	, frame_id_(0)
    {
      viewer_.registerKeyboardCallback (&RealSenseViewer::keyboardCallback, *this);
      bilateral_.setSigmaS (5);
    }

    ~RealSenseViewer ()
    {
      connection_.disconnect ();
    }

    void
    run ()
    {
      boost::function<void (const typename PointCloudT::ConstPtr&)> f = boost::bind (&RealSenseViewer::cloudCallback, this, _1);
      connection_ = grabber_.registerCallback (f);
      grabber_.start ();
      while (!viewer_.wasStopped ())
      {
        if (new_cloud_)
        {
          boost::mutex::scoped_lock lock (new_cloud_mutex_);
          if (!viewer_.updatePointCloud (new_cloud_, "cloud"))
          {
            viewer_.addPointCloud (new_cloud_, "cloud");
            viewer_.resetCamera ();
          }
          displaySettings ();
          last_cloud_ = new_cloud_;
          new_cloud_.reset ();
        }
        viewer_.spinOnce (1, true);
      }
      grabber_.stop ();
    }

  private:

    void
    cloudCallback (typename PointCloudT::ConstPtr cloud)
    {
      if (!viewer_.wasStopped ())
      {
        boost::mutex::scoped_lock lock (new_cloud_mutex_);
        if (with_bilateral_)
        {
          bilateral_.setInputCloud (cloud);
          typename PointCloudT::Ptr filtered (new PointCloudT);
          bilateral_.filter (*filtered);
          new_cloud_ = filtered;
        }
        else
        {
          new_cloud_ = cloud;
        }
		
		if (save_stream_)
		{
			savePointCloud(new_cloud_);
		}
			
      }
    }

    void
    keyboardCallback (const pcl::visualization::KeyboardEvent& event, void*)
    {
      if (event.keyDown ())
      {
        if (event.getKeyCode () == 'w' || event.getKeyCode () == 'W')
        {
          window_ += event.getKeyCode () == 'w' ? 1 : -1;
          if (window_ < 1)
            window_ = 1;
          pcl::console::print_info ("Temporal filtering window size: ");
          pcl::console::print_value ("%i\n", window_);
          grabber_.enableTemporalFiltering (temporal_filtering_, window_);
        }
        if (event.getKeyCode () == 't' || event.getKeyCode () == 'T')
        {
          threshold_ += event.getKeyCode () == 't' ? 1 : -1;
          if (threshold_ < 0)
            threshold_ = 0;
          if (threshold_ > 15)
            threshold_ = 15;
          pcl::console::print_info ("Confidence threshold: ");
          pcl::console::print_value ("%i\n", threshold_);
          grabber_.setConfidenceThreshold (threshold_);
        }
        if (event.getKeyCode () == 'k')
        {
          pcl::console::print_info ("Temporal filtering: ");
          switch (temporal_filtering_)
          {
            case pcl::RealSenseGrabber::RealSense_None:
              //{
                //temporal_filtering_ = pcl::RealSenseGrabber::RealSense_Median;
                //pcl::console::print_value ("median\n");
                //break;
              //}
            //case pcl::RealSenseGrabber::RealSense_Median:
              {
                temporal_filtering_ = pcl::RealSenseGrabber::RealSense_Average;
                pcl::console::print_value ("average\n");
                break;
              }
            case pcl::RealSenseGrabber::RealSense_Average:
              {
                temporal_filtering_ = pcl::RealSenseGrabber::RealSense_None;
                pcl::console::print_value ("none\n");
                break;
              }
          }
          grabber_.enableTemporalFiltering (temporal_filtering_, window_);
        }
        if (event.getKeyCode () == 'b')
        {
          with_bilateral_ = !with_bilateral_;
          pcl::console::print_info ("Bilateral filtering: ");
          pcl::console::print_value (with_bilateral_ ? "ON\n" : "OFF\n");
        }
        if (event.getKeyCode () == 'a' || event.getKeyCode () == 'A')
        {
          float s = bilateral_.getSigmaS ();
          s += event.getKeyCode () == 'a' ? 1 : -1;
          if (s <= 1)
            s = 1;
          pcl::console::print_info ("Bilateral filter spatial sigma: ");
          pcl::console::print_value ("%.0f\n", s);
          bilateral_.setSigmaS (s);
        }
        if (event.getKeyCode () == 'z' || event.getKeyCode () == 'Z')
        {
          float r = bilateral_.getSigmaR ();
          r += event.getKeyCode () == 'z' ? 0.01 : -0.01;
          if (r <= 0.01)
            r = 0.01;
          pcl::console::print_info ("Bilateral filter range sigma: ");
          pcl::console::print_value ("%.2f\n", r);
          bilateral_.setSigmaR (r);
        }
        if (event.getKeyCode () == 'p')
        {
          boost::format fmt ("RS_%s_%u.pcd");
          std::string fn = boost::str (fmt % grabber_.getDeviceSerialNumber ().c_str () % last_cloud_->header.stamp);
          pcl::io::savePCDFileBinaryCompressed (fn, *last_cloud_);
          pcl::console::print_info ("Saved point cloud: ");
          pcl::console::print_value (fn.c_str ());
          pcl::console::print_info ("\n");
        }
		if (event.getKeyCode() == 's')
		{
			save_stream_ = !save_stream_;
			pcl::console::print_info("Record stream: ");
			pcl::console::print_value(save_stream_ ? "ON\n" : "OFF\n");
			if (save_stream_)
			{
				stream_id_++; frame_id_++;
				createStreamDirectory();
			}
			else
			{
				frame_id_ = 0;
			}
			
		}
        displaySettings ();
      }
    }

	void createStreamDirectory()
	{
		std::stringstream ss;
		ss << std::setfill('0') << std::setw(4) << stream_id_;
		if (!CreateDirectory(ss.str().c_str(), NULL))
		{
			std::cerr << "Error creating save directory." << std::endl;
			exit(-1);
		}

	}
	
	void savePointCloud(typename PointCloudT::ConstPtr cloud)
	{
		std::stringstream ss;
		ss << std::setfill('0') << std::setw(4) << stream_id_ << "/" << std::setfill('0') << std::setw(4) << frame_id_ << ".pcd";
		pcl::io::savePCDFileBinaryCompressed(ss.str(), *cloud);
		frame_id_++;
	}

    void displaySettings ()
    {
      const int dx = 5;
      const int dy = 14;
      const int fs = 10;
      boost::format name_fmt ("text%i");
      const char* TF[] = {"off", "median", "average"};
      std::vector<boost::format> entries;
      // Framerate
      entries.push_back (boost::format ("framerate: %.1f") % grabber_.getFramesPerSecond ());
      // Confidence threshold
      entries.push_back (boost::format ("confidence threshold: %i") % threshold_);
      // Temporal filter settings
      std::string tfs = boost::str (boost::format (", window size %i") % window_);
      entries.push_back (boost::format ("temporal filtering: %s%s") % TF[temporal_filtering_] % (temporal_filtering_ == pcl::RealSenseGrabber::RealSense_None ? "" : tfs));
      // Bilateral filter settings
      std::string bfs = boost::str (boost::format ("spatial sigma %.0f, range sigma %.2f") % bilateral_.getSigmaS () % bilateral_.getSigmaR ());
      entries.push_back (boost::format ("bilateral filtering: %s") % (with_bilateral_ ? bfs : "off"));
	  // File io
	  entries.push_back(boost::format("save stream: %s") % (save_stream_ ? "on" : "off"));
	  for (size_t i = 0; i < entries.size (); ++i)
      {
        std::string name = boost::str (name_fmt % i);
        std::string entry = boost::str (entries[i]);
        if (!viewer_.updateText (entry, dx, dy + i * (fs + 2), fs, 1.0, 1.0, 1.0, name))
          viewer_.addText (entry, dx, dy + i * (fs + 2), fs, 1.0, 1.0, 1.0, name);
      }
    }

    pcl::RealSenseGrabber& grabber_;
    pcl::visualization::PCLVisualizer viewer_;
    boost::signals2::connection connection_;

    pcl::FastBilateralFilter<PointT> bilateral_;

    int window_;
    int threshold_;
    pcl::RealSenseGrabber::TemporalFilteringType temporal_filtering_;
    bool with_bilateral_;
	bool save_stream_;
	int stream_id_;
	int frame_id_;

    mutable boost::mutex new_cloud_mutex_;
    typename PointCloudT::ConstPtr new_cloud_;
    typename PointCloudT::ConstPtr last_cloud_;

};


int
main (int argc, char** argv)
{
  print_info ("Viewer for RealSense devices (run with --help for more information)\n", argv[0]);

  if (find_switch (argc, argv, "--help") || find_switch (argc, argv, "-h"))
  {
    printHelp (argc, argv);
    return (0);
  }

  if (find_switch (argc, argv, "--list") || find_switch (argc, argv, "-l"))
  {
    printDeviceList ();
    return (0);
  }

  bool xyz_only = find_switch (argc, argv, "--xyz");

  std::string device_id;

  if (argc == 1 ||             // no arguments
     (argc == 2 && xyz_only))  // single argument, and it is --xyz
  {
    device_id = "";
    print_info ("Creating a grabber for the first available device\n");
  }
  else
  {
    device_id = argv[argc - 1];
    print_info ("Creating a grabber for device \"%s\"\n", device_id.c_str ());
  }

  try
  {
    pcl::RealSenseGrabber grabber (device_id);
    if (xyz_only)
    {
      RealSenseViewer<pcl::PointXYZ> viewer (grabber);
      viewer.run ();
    }
    else
    {
      RealSenseViewer<pcl::PointXYZRGBA> viewer (grabber);
      viewer.run ();
    }
  }
  catch (pcl::io::IOException& e)
  {
    print_error ("Failed to create a grabber: %s\n", e.what ());
    return (1);
  }

  return (0);
}

