class RotateAroundSelf
{
	Actor @actor;
	Transform @transform;
	
	//= MISC ===================
	Vector3 m_startingRot;
	float m_speed = 10.0f;
	float m_rotation = 0.0f;
	//==========================
	
	// Constructor
	RotateAroundSelf(Actor @actorIn)
	{
		@actor = actorIn;
		@transform = actor.GetTransform();	
	}
	
	// Use this for initialization
	void Start()	
	{
		m_startingRot = transform.GetRotationLocal().ToEulerAngles();
		m_rotation = m_startingRot.y;
	}

	// Update is called once per frame
	void Update()
	{	
		float speed =  m_speed * time.GetDeltaTime();
		m_rotation += speed;
		
		Quaternion newRot = QuaternionFromEuler(m_startingRot.x, m_rotation, m_startingRot.z);	
		transform.SetRotationLocal(newRot);
	}
}