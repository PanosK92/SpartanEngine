class FirstPersonController
{
	GameObject @gameobject;
	Transform @transform;
	
	// wasd movement
	float movementAcceleration = 0.5f;
	float movementDeacceleration = 0.97f;
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
		if (input.GetKey(W))
			movementSpeed += movementAcceleration * transform.GetForward();
			
		// Move backward
		if (input.GetKey(S))
			movementSpeed -= movementAcceleration * transform.GetForward();
		
		// Move right
		if (input.GetKey(D))
			movementSpeed += movementAcceleration * transform.GetRight();
		
		// Move left
		if (input.GetKey(A))
			movementSpeed -= movementAcceleration * transform.GetRight();
		
		// Apply some drag
		movementSpeed *= movementDeacceleration;
		
		// Update the transform's position
		transform.Translate(movementSpeed * time.GetDeltaTime());
	}
}