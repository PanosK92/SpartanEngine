class FirstPersonController
{
	Actor @actor;
	Transform @transform;
	
	// wasd movement
	float acceleration = 1.0f;
	float drag = 0.8f;
	Vector3 movementSpeed = Vector3(0,0,0);

	// Constructor
	FirstPersonController(Actor @actorIn)
	{
		@actor = actorIn;
		@transform = actor.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		
	}

	// Update is called once per frame
	void Update()
	{
		if (input.GetButtonMouse(Right))
		{
			FPSMovement();
		}	
	}
	
	void FPSMovement()
	{
		// Move forward
		if (input.GetButtonKeyboard(W))
		{
			movementSpeed += acceleration * transform.GetForward() * time.GetDeltaTime();
		}		
		// Move backward
		if (input.GetButtonKeyboard(S))
		{
			movementSpeed -= acceleration * transform.GetForward() * time.GetDeltaTime();
		}
		// Move right
		if (input.GetButtonKeyboard(D))
		{
			movementSpeed += acceleration * transform.GetRight() * time.GetDeltaTime();
		}
		// Move left
		if (input.GetButtonKeyboard(A))
		{
			movementSpeed -= acceleration * transform.GetRight() * time.GetDeltaTime();
		}
		
		// Apply drag
		movementSpeed *= drag * (1.0f - time.GetDeltaTime());
		
		// Update the transform's position
		transform.Translate(movementSpeed);
	}
}