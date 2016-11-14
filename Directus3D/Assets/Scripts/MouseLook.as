class MouseLook
{
	GameObject @gameobject;
	Transform @transform;
	
	// mouse look settings
	float sensitivity = 0.1f;
	float yaw;
	float pitch;
	
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
		yaw = transform.GetRotation().ToEulerAngles().x;
		pitch = transform.GetRotation().ToEulerAngles().y;
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
		// Increment rotation by mouse delta
		yaw += input.GetMousePositionDelta().x * sensitivity;
		pitch += input.GetMousePositionDelta().y * sensitivity;

		// Clamp the top/bottom rotation freedom (and to avoid gimbal lock hehe)
		pitch = ClampRotation(pitch, 90);

		// Set the new rotation to the transform
		transform.SetRotationLocal(QuaternionFromEuler(pitch, yaw, 0.0f));
	}

	float ClampRotation(float rotation, float freedomAngles)
	{
		if (rotation > freedomAngles)
			rotation = freedomAngles;
		
		if (rotation < -freedomAngles)
			rotation = -freedomAngles;
			
		return rotation;
	}
}