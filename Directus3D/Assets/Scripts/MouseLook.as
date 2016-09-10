class MouseLook
{
	GameObject @gameobject;
	Transform @transform;
	
	// mouse look settings
	float sensitivity = 3.0f;
	float smoothing = 20.0f;

	// mouse look variables
	Vector2 smoothMouse = Vector2(0.0f, 0.0f);
	Vector3 currentRotation;
	
	// misc
	bool control = false;
	bool allowToggle = false;
	
	// Constructor
	MouseLook(GameObject @obj)
	{
		@gameobject = obj;
		@transform = gameobject.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		currentRotation = transform.GetRotation().ToEulerAngles();
	}

	// Update is called once per frame
	void Update()
	{	
		if (input.GetKey(E) && allowToggle)
		{
			control = !control;
			allowToggle = false;
		}
		else if (!input.GetKey(E))
		{
			allowToggle = true;
		}
		
		if (control)
			FreeLook();			
	}
	
	void FreeLook()
	{
		// Get raw mouse input
		Vector2 mouseDelta = Vector2(input.GetMousePositionDelta().x, input.GetMousePositionDelta().y);
	
		// Scale input against the sensitivity setting and multiply that against the smoothing value.
		mouseDelta.x *= sensitivity * smoothing * time.GetDeltaTime();
		mouseDelta.y *= sensitivity * smoothing * time.GetDeltaTime();
		
        // Interpolate mouse movement over time to apply smoothing delta.
		smoothMouse.x = Lerp(smoothMouse.x, mouseDelta.x, 1.0f / smoothing);
        smoothMouse.y = Lerp(smoothMouse.y, mouseDelta.y, 1.0f / smoothing);
		
		currentRotation.x += smoothMouse.x;
		currentRotation.y += smoothMouse.y;
		
		currentRotation.y = ClampRotation(currentRotation.y);
		
		Quaternion newRot = QuaternionFromEuler(currentRotation.y, currentRotation.x, 0.0f);
		
		transform.SetRotationLocal(newRot);
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