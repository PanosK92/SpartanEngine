class MouseLook
{
	Actor @actor;
	Transform @transform;
	
	// misc
	bool initialized = false;
	
	// mouse look settings
	float sensitivity = 0.1f;
	Vector3 currentRotation;
	
	
	// Constructor
	MouseLook(Actor @actorIn)
	{
		@actor = actorIn;
		@transform = actor.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		if (initialized)
			return;
			
		currentRotation.x = transform.GetRotation().ToEulerAngles().y;
		currentRotation.y = transform.GetRotation().ToEulerAngles().x;
		
		initialized = true;
	}

	// Update is called once per frame
	void Update()
	{	
		if (input.GetButtonMouse(Right))
		{
			FreeLook();
		}	
	}
	
	void FreeLook()
	{
		// Get raw mouse input
		float mouseDeltaX = input.GetMouseDelta().x;
		float mouseDeltaY = input.GetMouseDelta().y;
	
		currentRotation.y += mouseDeltaX * sensitivity;
		currentRotation.x += mouseDeltaY * sensitivity;
		
		// Limit top-bottom rotation freedom
		currentRotation.x = ClampRotation(currentRotation.x, -90.0f, 90.0f);
		
		// Set new rotation
		Quaternion newRot = QuaternionFromEuler(currentRotation);	
		transform.SetRotationLocal(newRot);
	}

	float ClampRotation(float rotation, float min, float max)
	{
		rotation = rotation > max ? max : rotation;
		rotation = rotation < min ? min : rotation;
			
		return rotation;
	}
}