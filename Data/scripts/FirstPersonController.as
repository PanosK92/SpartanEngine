class FirstPersonController
{
	Entity @entity;
	Transform @transform;
	
	// wasd movement
	float acceleration 		= 1.0f;
	float drag 				= acceleration * 0.8f;
	Vector3 movementSpeed 	= Vector3(0,0,0);

	// Constructor
	FirstPersonController(Entity @entityIn)
	{
		@entity 	= entityIn;
		@transform 	= entity.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		
	}

	// Update is called once per frame
	void Update(float delta_time)
	{
		if (input.GetKey(Click_Right))
		{
			FPSMovement(delta_time);
		}
		
		// Apply drag
		movementSpeed *= drag * (1.0f - delta_time);
	}
	
	void FPSMovement(float delta_time)
	{
		// Move forward
		if (input.GetKey(W))
		{
			movementSpeed += acceleration * transform.GetForward() * delta_time;
		}		
		// Move backward
		if (input.GetKey(S))
		{
			movementSpeed -= acceleration * transform.GetForward() * delta_time;
		}
		// Move right
		if (input.GetKey(D))
		{
			movementSpeed += acceleration * transform.GetRight() * delta_time;
		}
		// Move left
		if (input.GetKey(A))
		{
			movementSpeed -= acceleration * transform.GetRight() * delta_time;
		}
		
		// Update the transform's position
		transform.Translate(movementSpeed);
	}
}