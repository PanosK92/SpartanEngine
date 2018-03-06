class FirstPersonController
{
	GameObject @gameobject;
	Transform @transform;
	
	// wasd movement
	float acceleration = 0.5f;
	float drag = 0.99f;
	Vector3 movementSpeed = Vector3(0,0,0);

	// Constructor
	FirstPersonController(GameObject @obj)
	{
		@gameobject = obj;
		@transform = gameobject.GetTransform();
	}
	
	// Use this for initialization
	void Start()
	{
		
	}

	// Update is called once per frame
	void Update()
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