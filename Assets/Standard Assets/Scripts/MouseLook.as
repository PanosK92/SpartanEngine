class MouseLook
{
	Actor @actor;
	Transform @transform;
	
	// mouse look settings
	float sensitivity = 0.1f;
	Vector3 rotation;
	
	// Constructor
	MouseLook(Actor @actorIn)
	{
		@actor 		= actorIn;
		@transform 	= actor.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		rotation.y = transform.GetRotation().ToEulerAngles().x;
		rotation.x = transform.GetRotation().ToEulerAngles().y;
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
		rotation.y += input.GetMouseDelta().x * sensitivity;
		rotation.x += input.GetMouseDelta().y * sensitivity;
		
		// Clamp rotation along the x-axis
		rotation.x = rotation.x < -90.0f ? -90.0f : (rotation.x > 90.0f ? 90.0f : rotation.x);
	
		// Rotate
		transform.SetRotationLocal(Quaternion_FromEulerAngles(rotation));
	}
}