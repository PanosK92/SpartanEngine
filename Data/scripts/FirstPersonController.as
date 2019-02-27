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
	void Update()
	{
		if (input.GetKey(Click_Right))
		{
			FPSMovement();
		}	
	}
	
	void FPSMovement()
	{
		// Move forward
		if (input.GetKey(W))
		{
			movementSpeed += acceleration * transform.GetForward() * time.GetDeltaTime();
		}		
		// Move backward
		if (input.GetKey(S))
		{
			movementSpeed -= acceleration * transform.GetForward() * time.GetDeltaTime();
		}
		// Move right
		if (input.GetKey(D))
		{
			movementSpeed += acceleration * transform.GetRight() * time.GetDeltaTime();
		}
		// Move left
		if (input.GetKey(A))
		{
			movementSpeed -= acceleration * transform.GetRight() * time.GetDeltaTime();
		}
		
		// Apply drag
		movementSpeed *= drag * (1.0f - time.GetDeltaTime());
		
		// Update the transform's position
		transform.Translate(movementSpeed);
	}
}