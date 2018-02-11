class MouseLook
{
	GameObject @gameobject;
	Transform @transform;
	
	// mouse look settings
	float sensitivity = 30.0f;
	Vector3 currentRotation;
	
	// misc
	bool control = false;
	bool allowToggle = false;
	bool initialized = false;
	
	// Constructor
	MouseLook(GameObject @obj)
	{
		@gameobject = obj;
		@transform = gameobject.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		if (initialized)
			return;
			
		currentRotation = transform.GetRotation().ToEulerAngles();
		initialized = true;
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
		{
			FreeLook();
		}			
	}
	
	void FreeLook()
	{
		// Get raw mouse input
		float mouseDeltaX = input.GetMouseDelta().x;
		float mouseDeltaY = input.GetMouseDelta().y;
	
		currentRotation.x += mouseDeltaX * sensitivity * time.GetDeltaTime();
		currentRotation.y += mouseDeltaY * sensitivity * time.GetDeltaTime();
		
		// Limit top-bottom rotation freedom
		currentRotation.y = ClampRotation(currentRotation.y, -90.0f, 90.0f);
		
		// Set new rotation
		Quaternion newRot = QuaternionFromEuler(currentRotation.y, currentRotation.x, 0.0f);	
		transform.SetRotationLocal(newRot);
	}

	float ClampRotation(float rotation, float min, float max)
	{
		rotation = rotation > max ? max : rotation;
		rotation = rotation < min ? min : rotation;
			
		return rotation;
	}
}