#include "capture/RaspiCamera.h"

using namespace IL;

struct State {
	Camera* camera;
	VideoEncode* preview_encode;
	VideoEncode* record_encode;
	bool recording; // TODO : set this variable to true from an external thread to activate recording
	bool running;
	pthread_t preview_thid;
	pthread_t record_thid;
} state = { nullptr, nullptr, nullptr, false, false };

namespace kerberos
{
    void RaspiCamera::setup(kerberos::StringMap &settings)
    {
        int width = std::atoi(settings.at("captures.RaspiCamera.frameWidth").c_str());
        int height = std::atoi(settings.at("captures.RaspiCamera.frameHeight").c_str());
        int angle = std::atoi(settings.at("captures.RaspiCamera.angle").c_str());
        int delay = std::atoi(settings.at("captures.RaspiCamera.delay").c_str());

        // Initialize executor
        tryToUpdateCapture.setAction(this, &RaspiCamera::update);
        tryToUpdateCapture.setInterval("once at 1000 calls");

        // Save width and height in settings.
        Capture::setup(settings, width, height, angle);
        setImageSize(width, height);
        setRotation(angle);
        setDelay(delay);
        
        // Open camera
        open();
    }
    
    void RaspiCamera::grab()
    {
        try
        {
            pthread_mutex_lock(&m_lock);
            m_camera->grab();
            pthread_mutex_unlock(&m_lock);
        }
        catch(cv::Exception & ex)
        {
            pthread_mutex_unlock(&m_lock);
            throw OpenCVException(ex.msg.c_str());
        }
    }
    
    Image RaspiCamera::retrieve()
    {
        try
        {
            Image image;
            pthread_mutex_lock(&m_lock);
            m_camera->retrieve(image.getImage());
            pthread_mutex_unlock(&m_lock);
            return image;
        }
        catch(cv::Exception & ex)
        {
            pthread_mutex_unlock(&m_lock);
            throw OpenCVException(ex.msg.c_str());
        }
    }
    
    Image * RaspiCamera::takeImage()
    {
        // update the camera settings, with latest images
        //  - it's possible that we have to change the brightness, saturation, etc.
        tryToUpdateCapture();
        
        Image * image = new Image();

        try
        {
            // Delay camera for some time..
            usleep(m_delay*1000);

            // take an image 
            pthread_mutex_lock(&m_lock);
            m_camera->grab();
            m_camera->retrieve(image->getImage());
            pthread_mutex_unlock(&m_lock);

            // Check if need to rotate the image
            image->rotate(m_angle);
        }
        catch(cv::Exception & ex)
        {
            pthread_mutex_unlock(&m_lock);
            throw OpenCVException(ex.msg.c_str());
        }
        
        return image;
    }
    
    void RaspiCamera::setImageSize(int width, int height)
    {
        Capture::setImageSize(width, height);
        try
        {
            m_camera->set(CV_CAP_PROP_FORMAT, CV_8UC3);
            m_camera->set(CV_CAP_PROP_FRAME_WIDTH, m_frameWidth);
            m_camera->set(CV_CAP_PROP_FRAME_HEIGHT, m_frameHeight);
        }
        catch(cv::Exception & ex)
        {
            pthread_mutex_unlock(&m_lock);
            throw OpenCVException(ex.msg.c_str());
        }
    }
    
    void RaspiCamera::setRotation(int angle)
    {
        Capture::setRotation(angle);
    }
    
    void RaspiCamera::setDelay(int msec)
    {
        Capture::setDelay(msec);
    }
    
    void RaspiCamera::open()
    {
	// Initialize hardware
	bcm_host_init();

	// Create components
	state.camera = new Camera( 1280, 720, 0, false, 0, false );
	state.preview_encode = new VideoEncode( 8192, VideoEncode::CodingMJPEG, false, false );
	state.record_encode = new VideoEncode( 4096, VideoEncode::CodingAVC, false, false );

	// Setup camera
	state.camera->setFramerate( 30 );

	// Copy preview port definition to the encoder to help it handle incoming data
	Component::CopyPort( &state.camera->outputPorts()[70], &state.preview_encode->inputPorts()[200] );

	// Tunnel video port to AVC encoder
	state.camera->SetupTunnelVideo( state.record_encode );


	// Prepare components for next step
	state.camera->SetState( Component::StateIdle );
	state.preview_encode->SetState( Component::StateIdle );
	state.record_encode->SetState( Component::StateIdle );

	// Allocate buffers that will be processed manually
	state.camera->AllocateOutputBuffer( 70 );
	state.preview_encode->AllocateInputBuffer( 200 );

	// Start components
	state.camera->SetState( Component::StateExecuting );
	state.preview_encode->SetState( Component::StateExecuting );
	state.record_encode->SetState( Component::StateExecuting );

	// Start capturing
	state.camera->SetCapturing( true );
	state.running = true;

    }
    
    void RaspiCamera::close()
    {
        m_camera->release();
    }
    
    void RaspiCamera::update(){}
    
    bool RaspiCamera::isOpened()
    {
        return m_camera->isOpened();
    }
}
