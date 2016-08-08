class FirstPersonControllerPhysics
{
	GameObject @gameobject;
	Transform @transform;
	RigidBody @rigidbody;
	
	float movementSpeed = 10.0f;
	float jumpForce = 0.1f;
	
	// Constructor
	FirstPersonControllerPhysics(GameObject @obj)
	{
		@gameobject = obj;
	}
	
	// Use this for initialization
	void Start()
	{
		@transform = gameobject.GetTransform();
		@rigidbody = gameobject.GetRigidBody();
	}

	// Update is called once per frame
	void Update()
	{
		Movement();
	}
		
	void Movement()
	{
		// forward
		if (input.GetKey(W))
			rigidbody.ApplyForce(movementSpeed * transform.GetForward(), Force);
			
		// backward
		if (input.GetKey(S))
			rigidbody.ApplyForce(-movementSpeed * transform.GetForward(), Force);
		
		// right
		if (input.GetKey(D))
			rigidbody.ApplyForce(movementSpeed * transform.GetRight(), Force);
		
		// left
		if (input.GetKey(A))
			rigidbody.ApplyForce(-movementSpeed * transform.GetRight(), Force);
			
		// jump
		if (input.GetKey(Space))
			rigidbody.ApplyForce(jumpForce * Vector3(0, 1, 0), Impulse);
	}
}