class FirstPersonControllerPhysics
{
	GameObject @gameobject;
	Transform @transform;
	RigidBody @rigidbody;
	
	// rigidbody
	float movementSpeed = 10.0f;
	float jumpForce = 0.1f;
	
	// child camera
	Transform @cameraTransform;
	float sensitivity = 3.0f;
	float smoothing = 20.0f;
	Vector2 smoothMouse = Vector2(0.0f, 0.0f);
	Vector3 currentRotation;
	bool control = false;
	bool allowToggle = false;
	
	// Constructor
	FirstPersonControllerPhysics(GameObject @obj)
	{
		@gameobject = obj;
	}
	
	// Use this for initialization
	void Start()
	{
		@transform = gameobject.GetTransform();
		@rigidbody = gameobject.GetRigidBody();
		
		// Assuming that the camera GameObject is named "Camera"
		@cameraTransform = transform.GetChildByName("Camera");
	}

	// Update is called once per frame
	void Update()
	{
		if (input.GetButtonKeyboard(E) && allowToggle)
		{
			control = !control;
			allowToggle = false;
		}
		else if (!input.GetButtonKeyboard(E))
		{
			allowToggle = true;
		}
			
		if (control)
			MouseLook();			
			
		Movement();
	}
		
	void Movement()
	{
		// forward
		if (input.GetButtonKeyboard(W))
			rigidbody.ApplyForce(movementSpeed * cameraTransform.GetForward(), Force);
			
		// backward
		if (input.GetButtonKeyboard(S))
			rigidbody.ApplyForce(-movementSpeed * cameraTransform.GetForward(), Force);
		
		// right
		if (input.GetButtonKeyboard(D))
			rigidbody.ApplyForce(movementSpeed * cameraTransform.GetRight(), Force);
		
		// left
		if (input.GetButtonKeyboard(A))
			rigidbody.ApplyForce(-movementSpeed * cameraTransform.GetRight(), Force);
			
		// jump
		if (input.GetButtonKeyboard(Space))
			rigidbody.ApplyForce(jumpForce * Vector3(0,1,0), Impulse);
	}
	
	void MouseLook()
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
		
		cameraTransform.SetRotationLocal(QuaternionFromEuler(currentRotation.y, currentRotation.x, 0.0f));
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