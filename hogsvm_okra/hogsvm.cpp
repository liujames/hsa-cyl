#include <opencv2/opencv.hpp>

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <vector>
#include <time.h>
#include <sys/time.h>

#include "precomp.hpp"
/*
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
*/

using namespace cv;
using namespace cv::hsaml;
using namespace std;


void get_svm_detector(const Ptr<SVM>& svm, vector< float > & hog_detector );
void convert_to_ml(const std::vector< cv::Mat > & train_samples, cv::Mat& trainData );
void load_images( const string & prefix, const string & filename, vector< Mat > & img_lst );
void sample_neg( const vector< Mat > & full_neg_lst, vector< Mat > & neg_lst, const Size & size );
Mat get_hogdescriptor_visu(const Mat& color_origImg, vector<float>& descriptorValues, const Size & size );
void compute_hog( const vector< Mat > & img_lst, vector< Mat > & gradient_lst, const Size & size );
void train_svm( const vector< Mat > & gradient_lst, const vector< int > & labels );
void draw_locations( Mat & img, const vector< Rect > & locations, const Scalar & color );
void test_it( const Size & size );

void get_svm_detector(const Ptr<SVM>& svm, vector< float > & hog_detector )
{
    // get the support vectors
    Mat sv = svm->getSupportVectors();
    const int sv_total = sv.rows;
    // get the decision function
    Mat alpha, svidx;
    double rho = svm->getDecisionFunction(0, alpha, svidx);

    /*
    CV_Assert( alpha.total() == 1 && svidx.total() == 1 && sv_total == 1 );
    CV_Assert( (alpha.type() == CV_64F && alpha.at<double>(0) == 1.) ||
               (alpha.type() == CV_32F && alpha.at<float>(0) == 1.f) );
               */
    CV_Assert( sv.type() == CV_32F );
    hog_detector.clear();

    hog_detector.resize(sv.cols + 1);
    memcpy(&hog_detector[0], sv.ptr(), sv.cols*sizeof(hog_detector[0]));
    hog_detector[sv.cols] = (float)-rho;

}


/*
* Convert training/testing set to be used by OpenCV Machine Learning algorithms.
* TrainData is a matrix of size (#samples x max(#cols,#rows) per samples), in 32FC1.
* Transposition of samples are made if needed.
*/
void convert_to_ml(const std::vector< cv::Mat > & train_samples, cv::Mat& trainData )
{
    //--Convert data
    const int rows = (int)train_samples.size();
   const int cols = (int)std::max( train_samples[0].cols, train_samples[0].rows );

    cv::Mat tmp(1, cols, CV_32FC1); //< used for transposition if needed
    trainData = cv::Mat(rows, cols, CV_32FC1 );
    vector< Mat >::const_iterator itr = train_samples.begin();
    vector< Mat >::const_iterator end = train_samples.end();

    for( int i = 0 ; itr != end ; ++itr, ++i )
    {
        CV_Assert( itr->cols == 1 ||
            itr->rows == 1 );
        if( itr->cols == 1 )
        {
            transpose( *(itr), tmp );
            tmp.copyTo( trainData.row( i ) );
        }
        else if( itr->rows == 1 )
        {
            itr->copyTo( trainData.row( i ) );
        }
       // cout<<"row = "<<itr->rows<<" cols = "<<itr->cols<<endl;
    }


}

void load_images( const string & prefix, const string & filename, vector< Mat > & img_lst )
{
    string line;
    ifstream file;

    file.open( (prefix+filename).c_str() );
    if( !file.is_open() )
    {
        cout << "Unable to open the list of images from " << filename << " filename." << endl;
        exit( -1 );
    }

    bool end_of_parsing = false;
    while( !end_of_parsing )
    {
        getline( file, line );
        if( line == "" ) // no more file to read
        {
            end_of_parsing = true;
            break;
        }
        Mat img = imread( (prefix+line).c_str() ); // load the image
        if( img.empty() ) // invalid image, just skip it.
            continue;
#ifdef _DEBUG
        imshow( "image", img );
        waitKey( 10 );
#endif
        img_lst.push_back( img.clone() );
        cout<<"parsing "<<line<<endl;
    }
    cout<<"finish loading. "<<endl;
}

void sample_neg( const vector< Mat > & full_neg_lst, vector< Mat > & neg_lst, const Size & size )
{
    Rect box;
    box.width = size.width;
    box.height = size.height;

    const int size_x = box.width;
    const int size_y = box.height;

    srand( (unsigned int)time( NULL ) );

    vector< Mat >::const_iterator img = full_neg_lst.begin();
    vector< Mat >::const_iterator end = full_neg_lst.end();
    for( ; img != end ; ++img )
    {
        box.x = rand() % (img->cols - size_x);
        box.y = rand() % (img->rows - size_y);
        Mat roi = (*img)(box);
        neg_lst.push_back( roi.clone() );
#ifdef _DEBUG
        imshow( "img", roi.clone() );
        waitKey( 10 );
#endif
    }
}

// From http://www.juergenwiki.de/work/wiki/doku.php?id=public:hog_descriptor_computation_and_visualization
Mat get_hogdescriptor_visu(const Mat& color_origImg, vector<float>& descriptorValues, const Size & size )
{
    const int DIMX = size.width;
    const int DIMY = size.height;
    float zoomFac = 3;
    Mat visu;
    resize(color_origImg, visu, Size( (int)(color_origImg.cols*zoomFac), (int)(color_origImg.rows*zoomFac) ) );

    int cellSize        = 8;
    int gradientBinSize = 9;
    float radRangeForOneBin = (float)(CV_PI/(float)gradientBinSize); // dividing 180∞ into 9 bins, how large (in rad) is one bin?

    // prepare data structure: 9 orientation / gradient strenghts for each cell
    int cells_in_x_dir = DIMX / cellSize;
    int cells_in_y_dir = DIMY / cellSize;
    float*** gradientStrengths = new float**[cells_in_y_dir];
    int** cellUpdateCounter   = new int*[cells_in_y_dir];
    for (int y=0; y<cells_in_y_dir; y++)
    {
        gradientStrengths[y] = new float*[cells_in_x_dir];
        cellUpdateCounter[y] = new int[cells_in_x_dir];
        for (int x=0; x<cells_in_x_dir; x++)
        {
            gradientStrengths[y][x] = new float[gradientBinSize];
            cellUpdateCounter[y][x] = 0;

            for (int bin=0; bin<gradientBinSize; bin++)
                gradientStrengths[y][x][bin] = 0.0;
        }
    }

    // nr of blocks = nr of cells - 1
    // since there is a new block on each cell (overlapping blocks!) but the last one
    int blocks_in_x_dir = cells_in_x_dir - 1;
    int blocks_in_y_dir = cells_in_y_dir - 1;

    // compute gradient strengths per cell
    int descriptorDataIdx = 0;
    int cellx = 0;
    int celly = 0;

    for (int blockx=0; blockx<blocks_in_x_dir; blockx++)
    {
        for (int blocky=0; blocky<blocks_in_y_dir; blocky++)
        {
            // 4 cells per block ...
            for (int cellNr=0; cellNr<4; cellNr++)
            {
                // compute corresponding cell nr
                cellx = blockx;
                celly = blocky;
                if (cellNr==1) celly++;
                if (cellNr==2) cellx++;
                if (cellNr==3)
                {
                    cellx++;
                    celly++;
                }

                for (int bin=0; bin<gradientBinSize; bin++)
                {
                    float gradientStrength = descriptorValues[ descriptorDataIdx ];
                    descriptorDataIdx++;

                    gradientStrengths[celly][cellx][bin] += gradientStrength;

                } // for (all bins)


                // note: overlapping blocks lead to multiple updates of this sum!
                // we therefore keep track how often a cell was updated,
                // to compute average gradient strengths
                cellUpdateCounter[celly][cellx]++;

            } // for (all cells)


        } // for (all block x pos)
    } // for (all block y pos)


    // compute average gradient strengths
    for (celly=0; celly<cells_in_y_dir; celly++)
    {
        for (cellx=0; cellx<cells_in_x_dir; cellx++)
        {

            float NrUpdatesForThisCell = (float)cellUpdateCounter[celly][cellx];

            // compute average gradient strenghts for each gradient bin direction
            for (int bin=0; bin<gradientBinSize; bin++)
            {
                gradientStrengths[celly][cellx][bin] /= NrUpdatesForThisCell;
            }
        }
    }

    // draw cells
    for (celly=0; celly<cells_in_y_dir; celly++)
    {
        for (cellx=0; cellx<cells_in_x_dir; cellx++)
        {
            int drawX = cellx * cellSize;
            int drawY = celly * cellSize;

            int mx = drawX + cellSize/2;
            int my = drawY + cellSize/2;

            rectangle(visu, Point((int)(drawX*zoomFac), (int)(drawY*zoomFac)), Point((int)((drawX+cellSize)*zoomFac), (int)((drawY+cellSize)*zoomFac)), Scalar(100,100,100), 1);

            // draw in each cell all 9 gradient strengths
            for (int bin=0; bin<gradientBinSize; bin++)
            {
                float currentGradStrength = gradientStrengths[celly][cellx][bin];

                // no line to draw?
                if (currentGradStrength==0)
                    continue;

                float currRad = bin * radRangeForOneBin + radRangeForOneBin/2;

                float dirVecX = cos( currRad );
                float dirVecY = sin( currRad );
                float maxVecLen = (float)(cellSize/2.f);
                float scale = 2.5; // just a visualization scale, to see the lines better

                // compute line coordinates
                float x1 = mx - dirVecX * currentGradStrength * maxVecLen * scale;
                float y1 = my - dirVecY * currentGradStrength * maxVecLen * scale;
                float x2 = mx + dirVecX * currentGradStrength * maxVecLen * scale;
                float y2 = my + dirVecY * currentGradStrength * maxVecLen * scale;

                // draw gradient visualization
                line(visu, Point((int)(x1*zoomFac),(int)(y1*zoomFac)), Point((int)(x2*zoomFac),(int)(y2*zoomFac)), Scalar(0,255,0), 1);

            } // for (all bins)

        } // for (cellx)
    } // for (celly)


    // don't forget to free memory allocated by helper data structures!
    for (int y=0; y<cells_in_y_dir; y++)
    {
        for (int x=0; x<cells_in_x_dir; x++)
        {
            delete[] gradientStrengths[y][x];
        }
        delete[] gradientStrengths[y];
        delete[] cellUpdateCounter[y];
    }
    delete[] gradientStrengths;
    delete[] cellUpdateCounter;

    cout<<"end of compute visu. "<<line<<endl;

    return visu;

} // get_hogdescriptor_visu

void compute_hog( const vector< Mat > & img_lst, vector< Mat > & gradient_lst, const Size & size )
{
    HOGDescriptor hog;
    hog.winSize = size;
    Mat gray;
    vector< Point > location;
    vector< float > descriptors;
    int count=0;

    vector< Mat >::const_iterator img = img_lst.begin();
    vector< Mat >::const_iterator end = img_lst.end();
    for( ; img != end ; ++img )
    {
    	//cout<<"begin of hog img"<<endl;
        cvtColor( *img, gray, COLOR_BGR2GRAY );
        hog.compute( gray, descriptors, Size( 8, 8 ), Size( 0, 0 ), location );
        gradient_lst.push_back( Mat( descriptors ).clone() );
#ifdef _DEBUG
        imshow( "gradient", get_hogdescriptor_visu( img->clone(), descriptors, size ) );
        waitKey( 10 );
#endif

       // imshow( "gradient", get_hogdescriptor_visu( img->clone(), descriptors, size ) );
       // waitKey( 10 );
        //cout<<"end of hog img : "<<++count<<endl;
    }
}



void train_svm( const vector< Mat > & gradient_lst, const vector< int > & labels )
{
    /* Default values to train SVM */
    SVM::Params params;
    params.coef0 = 0.0;
    params.degree = 3;
    params.termCrit.epsilon = 1e-3;
    params.gamma = 0;
    params.kernelType = SVM::LINEAR;
    params.nu = 0.5;
    params.p = 0.1; // for EPSILON_SVR, epsilon in loss function?
    params.C = 0.01; // From paper, soft classifier
    params.svmType = SVM::EPS_SVR; // C_SVC; // EPSILON_SVR; // may be also NU_SVR; // do regression task

    Mat train_data;
    cout << "Start  convert_to_ml..."<<endl;
    convert_to_ml( gradient_lst, train_data );

    cout << "Start training..."<<endl;
    Ptr<SVM> svm = StatModel::train<SVM>(train_data, ROW_SAMPLE, Mat(labels), params);
    cout << "...[done]" << endl;

    svm->save( "people_detector.yml" );
}

void draw_locations( Mat & img, const vector< Rect > & locations, const Scalar & color )
{
    if( !locations.empty() )
    {
        vector< Rect >::const_iterator loc = locations.begin();
        vector< Rect >::const_iterator end = locations.end();
        for( ; loc != end ; ++loc )
        {
            rectangle( img, *loc, color, 2 );
        }
    }
}

void test_it( const Size & size )
{
    char key = 27;
    Scalar reference( 0, 255, 0 );
    Scalar trained( 0, 0, 0 );
    Mat img, draw;
    Ptr<SVM> svm;
    HOGDescriptor hog;
    HOGDescriptor my_hog;
    my_hog.winSize = size;
    VideoCapture video;
    vector< Rect > locations;

    // Load the trained SVM.


    fstream file("people_detector.yml", ios::in );      //宣告fstream物件
    if(file.is_open()){
    	cout<<"yml exists."<<endl;
        file.close();       //關閉檔案
    }


    svm = StatModel::load<SVM>( "people_detector.yml" );
    // Set the trained svm to my_hog
    vector< float > hog_detector;
    get_svm_detector( svm, hog_detector );
    my_hog.setSVMDetector( hog_detector );
    // Set the people detector.
    hog.setSVMDetector( hog.getDefaultPeopleDetector() );
    // Open the camera.
    video.open(0);
    if( !video.isOpened() )
    {
        cout << "Unable to open the device 0" << endl;
        exit( -1 );
    }

    bool end_of_process = false;
    while( !end_of_process )
    {
        video >> img;
        if( img.empty() )
            break;

        draw = img.clone();

        locations.clear();
        hog.detectMultiScale( img, locations );
        draw_locations( draw, locations, reference );

        locations.clear();
        my_hog.detectMultiScale( img, locations );
        draw_locations( draw, locations, trained );

        imshow( "Video", draw );
        key = (char)waitKey( 10 );
        if( 27 == key )
            end_of_process = true;
    }

}


int train_and_test()
{
	fstream f_perm;

	vector< Mat > pos_lst;
	vector< Mat > full_neg_lst;
	vector< Mat > neg_lst;
	vector< Mat > gradient_lst;
	vector< int > labels;

	String str_tmp="Performance Analysis2";
	timeval t1, t2;
	double elapsedTime;



    //load_images( "../data/INRIAPerson/", "Train/pos.lst", pos_lst );
    load_images( "../data/INRIAPerson/96X160H96/Train/pos/", "pos.lst", pos_lst );
//	load_images( "../data/INRIAPerson/Train/pos/", "pos.lst", pos_lst );
	labels.assign( pos_lst.size(), +1 );

    const unsigned int old = (unsigned int)labels.size();
    //load_images( "../data/INRIAPerson/", "Train/neg.lst", full_neg_lst );
    load_images( "../data/INRIAPerson/Train/neg/", "neg.lst", full_neg_lst );
    sample_neg( full_neg_lst, neg_lst, Size( 96,160 ) );
    labels.insert( labels.end(), neg_lst.size(), -1 );
    CV_Assert( old < labels.size() );

    cout<<"old = "<<old<<endl;
    cout<<"labels.size = "<<labels.size()<<endl;

    f_perm.open("perf.txt", ios::out );

    //開啟檔案為輸出狀態，若檔案已存在則清除檔案內容重新寫入

    f_perm<<"Performance Analysis : "<<endl;;   //將str寫入檔案

    gettimeofday(&t1, NULL);
    compute_hog( pos_lst, gradient_lst, Size( 96, 160 ) );
    gettimeofday(&t2, NULL);
    elapsedTime = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
    cout << elapsedTime << " s.\n";
    f_perm<<"compute_hog() : " << elapsedTime << " s.\n";

    gettimeofday(&t1, NULL);
    compute_hog( neg_lst, gradient_lst, Size( 96, 160 ) );
    gettimeofday(&t2, NULL);
    elapsedTime = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
    cout << elapsedTime << " s.\n";
    f_perm<<"compute_hog() : " << elapsedTime << " s.\n";


    gettimeofday(&t1, NULL);
    //train_svm( gradient_lst, labels );
    train_svm( gradient_lst, labels );
    gettimeofday(&t2, NULL);
    elapsedTime = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
    cout << elapsedTime << " s.\n";
    f_perm<<"train_svm() : " << elapsedTime << " s.\n";

    f_perm.close();       //關閉檔案


    test_it( Size( 96, 160 ) ); // change with your parameters


    return 0;
}



int test( int argc, char** argv )
{

    test_it( Size( 96, 160 ) ); // change with your parameters

    return 0;
}

int  main(){
	train_and_test();
	//test_it( Size( 96, 160 ) ); // change with your parameters
	return 0;

}

