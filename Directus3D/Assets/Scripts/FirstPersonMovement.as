class FirstPersonMovement
{
	GameObject @gameobject;
	Transform @transform;
	
	// wasd movement
	float movementAcceleration = 0.0025f;
	float movementDeacceleration = 0.9f;
	Vector3 movementSpeed = Vector3(0,0,0);
	
	// Constructor
	FirstPersonMovement(GameObject @obj)
	{
		@gameobject = obj;
	}
	
	// Use this for initialization
	void Start()
	{
		@transform = gameobject.GetTransform();
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
		
		// Update the current position of the transform
		Vector3 currentPos = transform.GetPositionLocal() + movementSpeed;
		
		// Update the transform's position
		transform.SetPositionLocal(currentPos);
	}
}