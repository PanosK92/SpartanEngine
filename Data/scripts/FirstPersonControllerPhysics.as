class FirstPersonControllerPhysics
{
	Entity @entity;
	Transform @transform;
	RigidBody @rigidbody;
	
	// rigidbody
	float movementSpeed = 10.0f;
	float jumpForce 	= 0.1f;
	
	// child camera
	Transform @cameraTransform;
	float sensitivity 			= 3.0f;
	float smoothing 			= 20.0f;
	Vector2 smoothMouse 		= Vector2(0.0f, 0.0f);
	Vector3 currentRotation;
	
	// Constructor
	FirstPersonControllerPhysics(Entity @entityIn)
	{
		@entity = entityIn;
	}
	
	// Use this for initialization
	void Start()
	{
		@transform = entity.GetTransform();
		@rigidbody = entity.GetRigidBody();
		
		// Assuming that the camera Entity is named "Camera"
		@cameraTransform = transform.GetChildByName("Camera");
	}

	// Update is called once per frame
	void Update()
	{
		if (input.GetKey(Click_Right))
		{
			FreeLook();
		}	
			
		Movement();
	}
		
	void Movement()
	{
		// forward
		if (input.GetKey(W))
			rigidbody.ApplyForce(movementSpeed * cameraTransform.GetForward(), Force);
			
		// backward
		if (input.GetKey(S))
			rigidbody.ApplyForce(-movementSpeed * cameraTransform.GetForward(), Force);
		
		// right
		if (input.GetKey(D))
			rigidbody.ApplyForce(movementSpeed * cameraTransform.GetRight(), Force);
		
		// left
		if (input.GetKey(A))
			rigidbody.ApplyForce(-movementSpeed * cameraTransform.GetRight(), Force);
			
		// jump
		if (input.GetKey(Space))
			rigidbody.ApplyForce(jumpForce * Vector3(0,1,0), Impulse);
	}
	
	void FreeLook()
	{
		// Get raw mouse input
		Vector2 mouseDelta = Vector2(input.GetMouseDelta().x, input.GetMouseDelta().y);
	
		// Scale input against the sensitivity setting and multiply that against the smoothing value.
		mouseDelta.x *= sensitivity * smoothing * time.GetDeltaTime();
		mouseDelta.y *= sensitivity * smoothing * time.GetDeltaTime();
		
        // Interpolate mouse movement over time to apply smoothing delta.
		smoothMouse.x = Lerp(smoothMouse.x, mouseDelta.x, 1.0f / smoothing);
        smoothMouse.y = Lerp(smoothMouse.y, mouseDelta.y, 1.0f / smoothing);
		
		// Calculate current rotation
		currentRotation.x += smoothMouse.x;
		currentRotation.y += smoothMouse.y;	
		currentRotation.y = ClampRotation(currentRotation.y);
		
		cameraTransform.SetRotationLocal(QuaternionFromEuler(Vector3(currentRotation.y, currentRotation.x, 0.0f)));
	}
	
	float ClampRotation(float rotation)
	{
		if (rotation > 90)
			rotation = 90;
		
		if (rotation < -90)
			rotation = -90;
			
		return rotation;
	}
}