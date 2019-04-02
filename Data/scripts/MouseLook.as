class MouseLook
{
	Entity @entity;
	Transform @transform;
	
	// mouse look settings
	float sensitivity = 0.1f;
	Vector3 rotation;
	
	// Constructor
	MouseLook(Entity @entityIn)
	{
		@entity 	= entityIn;
		@transform 	= entity.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		rotation	= transform.GetRotation().ToEulerAngles();
		rotation.z	= 0.0f;
	}

	// Update is called once per frame
	void Update()
	{	
		if (input.GetKey(Click_Right))
		{
			FreeLook();
		}	
	}
	
	void FreeLook()
	{
		// Get raw mouse delta
		Vector2 mouse_delta = input.GetMouseDelta();
		
		// Apply sensitivity
		mouse_delta *= sensitivity;
		
		// Compute rotation
		rotation.y += mouse_delta.x;
		rotation.x += mouse_delta.y;
		
		// Clamp rotation along the x-axis
		rotation.x = Clamp(rotation.x, -90.0f, 90.0f);
	
		// Rotate
		transform.SetRotationLocal(Quaternion_FromEulerAngles(rotation));
	}
}