class RotateAroundSelf
{
	Entity @entity;
	Transform @transform;
	
	//= MISC ===============
	Vector3 m_startingRot;
	float m_speed = 15.0f;
	float m_rotation = 0.0f;
	//======================
	
	// Constructor
	RotateAroundSelf(Entity @entityIn)
	{
		@entity		= entityIn;
		@transform	= entity.GetTransform();	
	}
	
	// Use this for initialization
	void Start()	
	{
		m_startingRot	= transform.GetRotationLocal().ToEulerAngles();
		m_rotation		= m_startingRot.y;
	}

	// Update is called once per frame
	void Update(float delta_time)
	{	
		float speed =  m_speed * delta_time;
		m_rotation	+= speed;
		
		Quaternion newRot = Quaternion_FromEulerAngles(Vector3(m_startingRot.x, m_rotation, m_startingRot.z));	
		transform.SetRotationLocal(newRot);
	}
}