class PingPongAlongX
{
	Entity @entity;
	Transform @transform;
	
	//= MISC ===================
	Vector3 m_currentPos;
	Quaternion m_currentRot;
	float m_speed = 0.1f;
	float m_distance = 0.0f;
	float m_maxDistance = 4.0f;
	//==========================
	
	// Constructor
	PingPongAlongX(Entity @entityIn)
	{
		@entity		= entityIn;
		@transform	= entity.GetTransform();	
	}
	
	// Use this for initialization
	void Start()	
	{
		m_currentPos	= transform.GetPositionLocal();
		m_currentRot	= transform.GetRotationLocal();
		m_distance		= m_maxDistance * 0.5f;
	}

	// Update is called once per frame
	void Update()
	{	
		float speed		= m_speed * time.GetDeltaTime();	
		m_currentPos	+= transform.GetForward() * speed;	
		m_distance		+= speed;
		
		if (m_distance > m_maxDistance || m_distance < -m_maxDistance)
		{
			m_speed *= -1;
		}
		
		transform.SetPositionLocal(m_currentPos);
	}
}